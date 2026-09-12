plugins { id("com.android.application") }

dependencies { testImplementation("junit:junit:4.13.2") }

// Native compilation is a separate incremental build, never a Gradle side effect.
val sdlSource = providers.gradleProperty("triaevumSdlSource")
val nativeStage = providers.gradleProperty("triaevumNativeStage")
android {
    namespace = "org.triaevum.android"
    compileSdk = 35
    defaultConfig {
        applicationId = "org.triaevum.android"
        minSdk = 29
        targetSdk = 35
        versionCode = 1
        versionName = "0.1-intro-dev"
        ndk { abiFilters += "arm64-v8a" }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    sourceSets.getByName("main") {
        if (sdlSource.isPresent) java.srcDir("${sdlSource.get()}/android-project/app/src/main/java")
        if (nativeStage.isPresent) jniLibs.srcDir(nativeStage.get())
    }
    packaging { jniLibs.useLegacyPackaging = true }
}
tasks.register("checkNativeStage") {
    doLast {
        check(sdlSource.isPresent && nativeStage.isPresent) {
            "Supply -PtriaevumSdlSource and -PtriaevumNativeStage from the Android native build."
        }
        for (name in listOf("SDL2", "TriAevum", "triaevum_title_bootstrap", "triaevum_title_aot", "c++_shared")) {
            check(file("${nativeStage.get()}/arm64-v8a/lib$name.so").isFile) { "Missing native library: $name" }
        }
    }
}
tasks.named("preBuild") { dependsOn("checkNativeStage") }
