// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Runs the Python Forge (tests/oracle/catalog_cases.py) on synthetic
//! fixtures built with its own test builders, then the Rust port on the same
//! files: accept/reject decisions, messages, returned values (insertion
//! order included) and output bytes (normalized RomFS, QM tables, COPY
//! programs and outputs, adaptation directories) must match.
//! Runs when TRIAEVUM_FORGE_PYTHON names a Python 3 interpreter.

use std::path::{Path, PathBuf};
use std::process::Command;

use forge_base::legacy_json::{dumps, Options, Style};
use forge_base::{CancelToken, ForgeError};
use forge_catalog::inputs::{InputFile, TitleInputs};
use forge_catalog::*;
use serde_json::{json, Map, Value};

const PYTHON_ORDER: Options = Options { style: Style::Spaced, sort_keys: false, ensure_ascii: true };
/// Python exception types whose text is an OS string, a codec message or a
/// traceback-only crash (KeyError/TypeError/AttributeError): the decision
/// must match, the message is documented as different.
const UNCOMPARED_MESSAGES: [&str; 7] =
    ["FileExistsError", "OSError", "UnicodeDecodeError", "KeyError", "TypeError", "AttributeError", "JSONDecodeError"];

struct Context {
    work: PathBuf,
    cancel: CancelToken,
}

impl Context {
    fn path(&self, args: &Value, key: &str) -> PathBuf {
        self.work.join(args[key].as_str().unwrap_or_else(|| panic!("{key} in {args}")))
    }

    fn json_file(&self, args: &Value, key: &str) -> Map<String, Value> {
        serde_json::from_slice::<Value>(&std::fs::read(self.path(args, key)).unwrap())
            .unwrap()
            .as_object()
            .unwrap()
            .clone()
    }

    fn relative(&self, path: &Path) -> String {
        let root = forge_catalog::paths::resolve(&self.work);
        let path = forge_catalog::paths::resolve(path);
        path.strip_prefix(&root)
            .unwrap()
            .components()
            .map(|c| c.as_os_str().to_string_lossy())
            .collect::<Vec<_>>()
            .join("/")
    }

    fn extracted(&self, args: &Value) -> TitleInputs {
        let file = |kind: &str| {
            let row = &args["files"][kind];
            InputFile {
                path: self.work.join(row[0].as_str().unwrap()),
                bytes: row[1].as_u64().unwrap(),
                sha256: row[2].as_str().unwrap().to_string(),
            }
        };
        TitleInputs {
            program_id: args["program_id"].as_u64().unwrap(),
            code: file("code"),
            exheader: file("exheader"),
            romfs: file("romfs"),
        }
    }

    fn extracted_args(&self, inputs: &TitleInputs) -> Value {
        let files: Map<String, Value> = inputs
            .by_kind()
            .iter()
            .map(|(kind, file)| (kind.to_string(), json!([self.relative(&file.path), file.bytes, file.sha256])))
            .collect();
        json!({"program_id": inputs.program_id, "files": files})
    }
}

fn same_bytes(left: &Path, right: &Path, failures: &mut Vec<String>) {
    match (std::fs::read(left), std::fs::read(right)) {
        (Ok(a), Ok(b)) if a == b => {}
        (Ok(a), Ok(b)) => {
            let at = a.iter().zip(&b).position(|(x, y)| x != y).unwrap_or(a.len().min(b.len()));
            failures.push(format!(
                "{} differs from {} at byte {at} (sizes {} / {})",
                right.display(),
                left.display(),
                a.len(),
                b.len()
            ));
        }
        (a, b) => failures.push(format!("{} / {}: {:?} {:?}", left.display(), right.display(), a.is_ok(), b.is_ok())),
    }
}

fn same_directory(python: &Path, rust: &Path, failures: &mut Vec<String>) {
    let names = |directory: &Path| {
        let mut names: Vec<String> = std::fs::read_dir(directory)
            .map(|entries| entries.map(|e| e.unwrap().file_name().to_string_lossy().into_owned()).collect())
            .unwrap_or_default();
        names.sort();
        names
    };
    let (expected, actual) = (names(python), names(rust));
    if expected != actual {
        failures.push(format!("{}: files {actual:?}, Python {expected:?}", rust.display()));
        return;
    }
    for name in expected {
        same_bytes(&python.join(&name), &rust.join(&name), failures);
    }
}

/// Runs one case in Rust; byte comparisons of written files go to `failures`.
fn run(context: &Context, op: &str, args: &Value, failures: &mut Vec<String>) -> Result<Value, ForgeError> {
    let cancel = &context.cancel;
    match op {
        "normalize" => {
            let output = context.path(args, "rust_output");
            let value = region_assets::normalize_romfs(&context.path(args, "source"), &output, cancel)?;
            same_bytes(&context.path(args, "output"), &output, failures);
            Ok(value)
        }
        "fingerprint" => Ok(fingerprint(&context.path(args, "path"), cancel)?.to_value()),
        "qm" => {
            let data = std::fs::read(context.path(args, "data")).unwrap();
            let table = region_assets::adapt_qm_table(&data, args["file_size"].as_i64().unwrap().max(0) as u64)?;
            if std::fs::read(context.path(args, "output")).unwrap() != table {
                failures.push(format!("QM table differs: {args}"));
            }
            Ok(args["output"].clone())
        }
        "copy_build" => {
            let source = std::fs::read(context.path(args, "source")).unwrap();
            let canonical = std::fs::read(context.path(args, "canonical")).unwrap();
            let program = copy_adapter::build_program(&source, &canonical)?;
            if std::fs::read(context.path(args, "program_file")).unwrap()
                != forge_base::legacy_json::pretty_file_bytes(&program)
            {
                failures.push(format!("COPY program file bytes differ: {args}"));
            }
            Ok(program)
        }
        "copy_apply" => {
            let source = std::fs::read(context.path(args, "source")).unwrap();
            let program = Value::Object(context.json_file(args, "program"));
            let output = copy_adapter::apply_program(&source, &program)?;
            if std::fs::read(context.path(args, "output")).unwrap() != output {
                failures.push(format!("COPY output differs: {args}"));
            }
            Ok(args["output"].clone())
        }
        "import_contract" => import_contract(&context.json_file(args, "recipe")),
        "adapter_validate" => {
            Ok(validate_adapter(&context.path(args, "root"), &context.json_file(args, "recipe"), cancel)?
                .unwrap_or(Value::Null))
        }
        "adapt" => {
            let extracted = context.extracted(&args["extracted"]);
            let recipe = context.json_file(args, "recipe");
            let output = context.path(args, "rust_output");
            let result = adapt_extracted_inputs(&extracted, &recipe, &context.path(args, "root"), &output, cancel);
            same_directory(&context.path(args, "output"), &output, failures);
            let value = match result? {
                None => json!({"inputs": context.extracted_args(&extracted), "receipt": null}),
                Some(adaptation) => {
                    json!({"inputs": context.extracted_args(&adaptation.inputs), "receipt": adaptation.receipt})
                }
            };
            // Python wrote to py_out, Rust to rs_out.
            Ok(serde_json::from_str(&dumps(&value, PYTHON_ORDER).replace("/rs_out/", "/py_out/")).unwrap())
        }
        "match" => {
            let recipe =
                match_extracted_recipe(&context.extracted(&args["extracted"]), &context.path(args, "recipes"), cancel)?;
            Ok(Value::Object(recipe))
        }
        "family_expected" => {
            let phase = args["phase"].as_str().unwrap();
            Ok(family::expected(&context.json_file(args, "recipe"), phase)?.unwrap_or(Value::Null))
        }
        "family_identity" | "family_verify" => {
            let (code, exheader, romfs) =
                (context.path(args, "code"), context.path(args, "exheader"), context.path(args, "romfs"));
            let paths = InputPaths { code: &code, exheader: &exheader, romfs: &romfs };
            if op == "family_identity" {
                family::identity(paths, cancel)
            } else {
                family::verify(&context.json_file(args, "recipe"), paths, args["phase"].as_str().unwrap(), cancel)
            }
        }
        "options" => Ok(Value::Array(
            recipes::load_recipe_options(&context.path(args, "path"))?
                .into_iter()
                .map(|o| json!([o.id, o.label]))
                .collect(),
        )),
        "load_recipe" => Ok(Value::Object(load_recipe(&context.path(args, "path"), args["id"].as_str().unwrap())?)),
        "verify_input" => {
            let contract = args["contract"].as_object().unwrap();
            let verified =
                verify_input("code", &context.path(args, "path"), contract, &mut recipes::FreshDigest(cancel))?;
            let mut value = verified.as_json();
            value.as_object_mut().unwrap().shift_remove("path");
            Ok(value)
        }
        "checked_file" => {
            let root = context.path(args, "root");
            let path = checked_file(&root, &args["record"], cancel)?;
            let root = forge_catalog::paths::resolve(&root);
            Ok(json!(path
                .strip_prefix(&root)
                .unwrap()
                .components()
                .map(|c| c.as_os_str().to_string_lossy())
                .collect::<Vec<_>>()
                .join("/")))
        }
        "load_catalog" => Ok(Value::Object(load_catalog(&context.path(args, "root"))?)),
        "validate_title" => Ok(Value::Object(validate_title(
            &context.path(args, "root"),
            &context.json_file(args, "recipe"),
            None,
            cancel,
        )?)),
        "select_title" => {
            let target = host_platform()?;
            Ok(Value::Object(select_title(
                &context.path(args, "root"),
                &context.json_file(args, "recipe"),
                None,
                &target,
                cancel,
            )?))
        }
        "runtime_record" => {
            let root = context.path(args, "root");
            let catalog = forge_base::fsutil::load_json_object(&root.join("recipes/precompiled-titles.json"))?;
            runtime_record(&root, &catalog)
        }
        "variants" => {
            let (catalog, recipes) =
                variants::add_verified_variants(&args["catalog"], &args["recipes"], &args["definitions"])?;
            Ok(json!([catalog, recipes]))
        }
        "bind" => {
            let root = PathBuf::from(args["root_abs"].as_str().unwrap());
            let (catalog, recipes, files) = coverage::bind(&args["catalog"], &args["recipes"], &root, cancel)?;
            Ok(json!([catalog, recipes, files]))
        }
        "require_coverage" => {
            let root = PathBuf::from(args["root_abs"].as_str().unwrap());
            coverage::require_coverage(&args["catalog"], &args["recipes"], &root)?;
            Ok(Value::Null)
        }
        other => panic!("unknown oracle op {other}"),
    }
}

#[test]
fn catalog_matches_python() {
    let Ok(python) = std::env::var("TRIAEVUM_FORGE_PYTHON") else {
        eprintln!("skipped: TRIAEVUM_FORGE_PYTHON is not set");
        return;
    };
    let manifest_directory = Path::new(env!("CARGO_MANIFEST_DIR"));
    let repository = manifest_directory.join("../../../..");
    let release = repository.join("tools/triaevum_release");
    let directory = tempfile::tempdir().unwrap();
    let work = directory.path().join("work");
    std::fs::create_dir(&work).unwrap();
    let status = Command::new(python)
        .arg("-I")
        .arg(manifest_directory.join("tests/oracle/catalog_cases.py"))
        .args([&release, &repository, &work])
        .status()
        .unwrap();
    assert!(status.success());
    let manifest: Value = serde_json::from_slice(&std::fs::read(work.join("manifest.json")).unwrap()).unwrap();
    let context = Context { work, cancel: CancelToken::new() };
    let mut failures = Vec::new();
    let cases = manifest["cases"].as_array().unwrap();
    for case in cases {
        let (op, args, expect) = (case["op"].as_str().unwrap(), &case["args"], &case["expect"]);
        let actual = run(&context, op, args, &mut failures);
        match (expect["ok"].as_bool().unwrap(), actual) {
            (true, Ok(value)) => {
                let text = dumps(&value, PYTHON_ORDER);
                if text != expect["text"].as_str().unwrap() {
                    failures.push(format!("{op} {args}: value\n  rust   {text}\n  python {}", expect["text"]));
                }
            }
            (false, Err(error)) => {
                let kind = expect["type"].as_str().unwrap();
                if !UNCOMPARED_MESSAGES.contains(&kind) && error.message != expect["message"].as_str().unwrap() {
                    failures.push(format!(
                        "{op} {args}: message\n  rust   {}\n  python {kind}: {}",
                        error.message, expect["message"]
                    ));
                }
            }
            (true, Err(error)) => {
                failures.push(format!("{op} {args}: Rust rejected ({}), Python accepted", error.message))
            }
            (false, Ok(value)) => failures.push(format!(
                "{op} {args}: Rust accepted ({}), Python rejected with {}: {}",
                dumps(&value, PYTHON_ORDER),
                expect["type"],
                expect["message"]
            )),
        }
    }
    let count = failures.len();
    failures.truncate(25);
    assert!(failures.is_empty(), "{count} oracle mismatches:\n{}", failures.join("\n"));
    assert!(cases.len() > 1500, "{} cases", cases.len());
}
