plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

dependencies {
    // The Azahar touch overlay (ports/android/controls).
    implementation(project(":controls"))
    testImplementation("junit:junit:4.13.2")
}

// Native compilation is a separate incremental build, never a Gradle side effect.
val sdlSource = providers.gradleProperty("triaevumSdlSource")
val nativeStage = providers.gradleProperty("triaevumNativeStage")
// The publisher's Forge package for this APK: a folder holding forge-package/ (catalog,
// recipes, resources, tables, shader corpus). Optional for developer APKs.
val forgePackage = providers.gradleProperty("triaevumForgePackage")

// Release APKs (owner directive 2026-11-09): one flavor per title. The title facts
// (application id, label, version) are read from titles/<title>/android.json, the
// file tools/triaevum_release/android_release_policy.py audits against; a missing
// or invalid fact fails the configuration. The `dev` flavor keeps the developer
// APK's identity (org.triaevum.android, 0.1-intro-dev) and has no release variant.
val titlesRoot = rootProject.file("../../titles")
fun androidFacts(title: String): Map<String, String> {
    val file = titlesRoot.resolve("$title/android.json")
    if (!file.isFile) throw GradleException("Missing Android title facts: titles/$title/android.json")
    @Suppress("UNCHECKED_CAST")
    val facts = groovy.json.JsonSlurper().parse(file) as Map<String, Any?>
    return listOf("title", "flavor", "application_id", "label", "version_name").associateWith { key ->
        (facts[key] as? String)?.takeIf { it.isNotEmpty() && (key != "title" || it == title) }
            ?: throw GradleException("Invalid Android title fact $key in titles/$title/android.json")
    }
}
// M*10^7 + m*10^5 + p*10^3 + stage*100 + n (alpha 1, beta 2, rc 3, final 9), as
// android_release_policy.version_code.
fun androidVersionCode(name: String): Int {
    val match = Regex("""^(\d+)\.(\d+)\.(\d+)(?:-(alpha|beta|rc)\.(\d+))?$""").matchEntire(name)
        ?: throw GradleException("Unsupported Android version name: $name")
    val (major, minor, patch, stage, number) = match.destructured
    val stageCode = mapOf("alpha" to 1, "beta" to 2, "rc" to 3, "" to 9).getValue(stage)
    val n = number.ifEmpty { "0" }.toInt()
    if (minor.toInt() > 99 || patch.toInt() > 99 || n > 99) throw GradleException("Version out of range: $name")
    return major.toInt() * 10_000_000 + minor.toInt() * 100_000 + patch.toInt() * 1_000 + stageCode * 100 + n
}
val releaseTitles = mapOf("eot" to "oot3d", "mc" to "mm3d")
// Signing reads the four variables only from the environment; there is no
// fallback to the debug key. -PtriaevumUnsignedReleaseCandidate=true builds an
// unsigned candidate instead (the Android plugin names it *-release-unsigned.apk).
val signingVariables = listOf("TRIAEVUM_ANDROID_KEYSTORE", "TRIAEVUM_ANDROID_KEYSTORE_PASS",
    "TRIAEVUM_ANDROID_KEY_ALIAS", "TRIAEVUM_ANDROID_KEY_PASS")
val signingEnvironment = signingVariables.associateWith { System.getenv(it) }
val unsignedCandidate = providers.gradleProperty("triaevumUnsignedReleaseCandidate").map { it == "true" }.getOrElse(false)
val canSign = signingEnvironment.values.all { !it.isNullOrEmpty() }

android {
    namespace = "org.triaevum.android"
    compileSdk = 35
    defaultConfig {
        minSdk = 29
        targetSdk = 35
        ndk { abiFilters += "arm64-v8a" }
    }
    signingConfigs {
        create("release") {
            if (canSign) {
                storeFile = file(signingEnvironment.getValue("TRIAEVUM_ANDROID_KEYSTORE")!!)
                storePassword = signingEnvironment.getValue("TRIAEVUM_ANDROID_KEYSTORE_PASS")
                keyAlias = signingEnvironment.getValue("TRIAEVUM_ANDROID_KEY_ALIAS")
                keyPassword = signingEnvironment.getValue("TRIAEVUM_ANDROID_KEY_PASS")
                enableV2Signing = true
                enableV3Signing = true
            }
        }
    }
    flavorDimensions += "title"
    productFlavors {
        create("dev") {
            dimension = "title"
            applicationId = "org.triaevum.android"
            versionCode = 1
            versionName = "0.1-intro-dev"
        }
        for ((flavor, title) in releaseTitles) {
            val facts = androidFacts(title)
            if (facts.getValue("flavor") != flavor) throw GradleException("titles/$title/android.json names another flavor")
            create(flavor) {
                dimension = "title"
                applicationId = facts.getValue("application_id")
                versionName = facts.getValue("version_name")
                versionCode = androidVersionCode(facts.getValue("version_name"))
            }
        }
    }
    buildTypes {
        getByName("release") {
            isDebuggable = false
            isJniDebuggable = false
            isMinifyEnabled = false
            signingConfig = if (canSign && !unsignedCandidate) signingConfigs.getByName("release") else null
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    sourceSets.getByName("main") {
        if (sdlSource.isPresent) java.srcDir("${sdlSource.get()}/android-project/app/src/main/java")
        if (nativeStage.isPresent) jniLibs.srcDir(nativeStage.get())
        if (forgePackage.isPresent) assets.srcDir(forgePackage.get())
    }
    packaging {
        // Extracted, unchanged libraries: Forge verifies them in nativeLibraryDir
        // against the catalog's hashes of the staged (already stripped) files.
        jniLibs.useLegacyPackaging = true
        jniLibs.keepDebugSymbols += "**/*.so"
    }
}
androidComponents {
    // The developer identity never ships as a release.
    beforeVariants(selector().withFlavor("title" to "dev").withBuildType("release")) { it.enable = false }
}
// Fail closed: a release task without the signing environment (and without the
// explicit unsigned-candidate switch) stops before anything is built.
gradle.taskGraph.whenReady {
    val release = allTasks.any { it.project == project && it.name.contains("Release") }
    if (release && !canSign && !unsignedCandidate) {
        val missing = signingVariables.filter { signingEnvironment[it].isNullOrEmpty() }
        throw GradleException("Release signing needs ${missing.joinToString()} in the environment " +
            "(or -PtriaevumUnsignedReleaseCandidate=true for an unsigned candidate).")
    }
}
tasks.register("checkNativeStage") {
    doLast {
        check(sdlSource.isPresent && nativeStage.isPresent) {
            "Supply -PtriaevumSdlSource and -PtriaevumNativeStage from the Android native build."
        }
        for (name in listOf("SDL2", "TriAevum", "triaevum_title_bootstrap", "triaevum_title_aot", "triaevum_forge", "c++_shared")) {
            check(file("${nativeStage.get()}/arm64-v8a/lib$name.so").isFile) { "Missing native library: $name" }
        }
    }
}
tasks.named("preBuild") { dependsOn("checkNativeStage") }
