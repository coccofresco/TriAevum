// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! CPython 3.12+ float semantics the scene-light analysis depends on. Its
//! catalog bytes are float reprs, so "the same formula" is not enough: the
//! builtins below reproduce CPython's operation order and rounding.

/// `sum(values)` with the default int start: the first float is added to
/// int 0 (so -0.0 becomes 0.0), the rest by CPython's Neumaier compensated
/// summation (`builtin_sum_impl`, Python 3.12+). Empty sums are 0.
pub fn sum(values: impl IntoIterator<Item = f64>) -> f64 {
    let mut values = values.into_iter();
    let Some(first) = values.next() else { return 0.0 };
    let mut total = 0.0 + first;
    let mut compensation = 0.0;
    for x in values {
        let t = total + x;
        if total.abs() >= x.abs() {
            compensation += (total - t) + x;
        } else {
            compensation += (x - t) + total;
        }
        total = t;
    }
    if compensation != 0.0 && compensation.is_finite() {
        total += compensation;
    }
    total
}

/// `max(values)` of a nonempty sequence: the first of equal values,
/// replaced only by a greater one.
pub fn max(values: impl IntoIterator<Item = f64>) -> f64 {
    let mut values = values.into_iter();
    let first = values.next().expect("max() of an empty sequence");
    values.fold(first, max2)
}

/// `min(values)` of a nonempty sequence.
pub fn min(values: impl IntoIterator<Item = f64>) -> f64 {
    let mut values = values.into_iter();
    let first = values.next().expect("min() of an empty sequence");
    values.fold(first, min2)
}

/// `max(a, b)`: `a` unless `b` is greater.
pub fn max2(a: f64, b: f64) -> f64 {
    if b > a {
        b
    } else {
        a
    }
}

/// `min(a, b)`: `a` unless `b` is smaller.
pub fn min2(a: f64, b: f64) -> f64 {
    if b < a {
        b
    } else {
        a
    }
}

/// Python's ValueError (NaN) or OverflowError (infinity) from `round()`,
/// `int()` or a math function given a value outside its domain.
#[derive(Debug)]
pub struct NotFinite;

/// `round(x)` (no ndigits): the nearest integer, ties to even.
pub fn round_int(x: f64) -> Result<i64, NotFinite> {
    if !x.is_finite() {
        return Err(NotFinite);
    }
    let rounded = x.round_ties_even();
    if rounded.abs() >= 9.2e18 {
        return Err(NotFinite);
    }
    Ok(rounded as i64)
}

/// `round(x, ndigits)` for small positive ndigits: the exact binary value
/// rounded to `ndigits` decimals (ties to even, as `_Py_dg_dtoa` mode 3)
/// and read back to the nearest double, like CPython's `double_round`.
pub fn round_digits(x: f64, ndigits: usize) -> f64 {
    if !x.is_finite() || x == 0.0 {
        return x;
    }
    format!("{x:.ndigits$}").parse().expect("a formatted float parses")
}

/// `x // y` for floats (`_float_div_mod`), including its snapping of the
/// quotient to the nearest integer.
pub fn floor_div(x: f64, y: f64) -> f64 {
    let modulo = x % y;
    let mut div = (x - modulo) / y;
    if modulo != 0.0 && (y < 0.0) != (modulo < 0.0) {
        div -= 1.0;
    }
    if div != 0.0 {
        let mut floor = div.floor();
        if div - floor > 0.5 {
            floor += 1.0;
        }
        floor
    } else {
        0.0f64.copysign(x / y)
    }
}

/// `x % y` for floats: the remainder takes the divisor's sign.
pub fn modulo(x: f64, y: f64) -> f64 {
    let result = x % y;
    if result == 0.0 {
        0.0f64.copysign(y)
    } else if (y < 0.0) != (result < 0.0) {
        result + y
    } else {
        result
    }
}

/// `x ** 2` through libm `pow`, as `float_pow` calls it after its own sign,
/// zero and one handling. The exponent is opaque to the optimizer so the
/// call is not rewritten into a multiplication.
pub fn square_pow(x: f64) -> f64 {
    let base = x.abs();
    if x.is_nan() || base == 0.0 || base == 1.0 {
        return if x.is_nan() { x } else { base };
    }
    base.powf(std::hint::black_box(2.0))
}

/// `math.cos`: ValueError for infinite arguments.
pub fn cos(x: f64) -> Result<f64, NotFinite> {
    if x.is_infinite() {
        Err(NotFinite)
    } else {
        Ok(x.cos())
    }
}

/// `math.sin`: ValueError for infinite arguments.
pub fn sin(x: f64) -> Result<f64, NotFinite> {
    if x.is_infinite() {
        Err(NotFinite)
    } else {
        Ok(x.sin())
    }
}

/// `math.hypot(*values)`.
pub fn hypot(values: &[f64]) -> f64 {
    let mut coordinates: Vec<f64> = values.iter().map(|v| v.abs()).collect();
    norm(&mut coordinates)
}

/// `math.dist(p, q)`.
pub fn dist(p: &[f64], q: &[f64]) -> f64 {
    let mut coordinates: Vec<f64> = p.iter().zip(q).map(|(a, b)| (a - b).abs()).collect();
    norm(&mut coordinates)
}

fn norm(coordinates: &mut [f64]) -> f64 {
    let mut max = 0.0;
    let mut found_nan = false;
    for &x in coordinates.iter() {
        found_nan |= x.is_nan();
        if x > max {
            max = x;
        }
    }
    vector_norm(coordinates, max, found_nan)
}

/// CPython's `vector_norm` (mathmodule.c, 3.12+): lossless scaling and
/// squaring, compensated accumulation and one differential correction.
fn vector_norm(vec: &mut [f64], max: f64, found_nan: bool) -> f64 {
    if max.is_infinite() {
        return max;
    }
    if found_nan {
        return f64::NAN;
    }
    if max == 0.0 || vec.len() <= 1 {
        return max;
    }
    let max_e = frexp_exponent(max);
    if max_e < -1023 {
        for x in vec.iter_mut() {
            *x /= f64::MIN_POSITIVE;
        }
        return f64::MIN_POSITIVE * vector_norm(vec, max / f64::MIN_POSITIVE, found_nan);
    }
    let scale = ldexp_one(-max_e);
    let mut csum = 1.0;
    let mut frac1 = 0.0;
    let mut frac2 = 0.0;
    for &x in vec.iter() {
        let x = x * scale;
        let (hi, lo) = mul_exact(x, x);
        let (sum_hi, sum_lo) = fast_sum(csum, hi);
        csum = sum_hi;
        frac1 += lo;
        frac2 += sum_lo;
    }
    let mut h = (csum - 1.0 + (frac1 + frac2)).sqrt();
    let (hi, lo) = mul_exact(-h, h);
    let (sum_hi, sum_lo) = fast_sum(csum, hi);
    csum = sum_hi;
    frac1 += lo;
    frac2 += sum_lo;
    let x = csum - 1.0 + (frac1 + frac2);
    h += x / (2.0 * h);
    h / scale
}

fn fast_sum(a: f64, b: f64) -> (f64, f64) {
    let x = a + b;
    (x, (a - x) + b)
}

fn mul_exact(x: f64, y: f64) -> (f64, f64) {
    let z = x * y;
    (z, x.mul_add(y, -z))
}

/// The exponent `frexp` returns for a positive finite value.
fn frexp_exponent(value: f64) -> i32 {
    let exponent = ((value.to_bits() >> 52) & 0x7ff) as i32;
    if exponent == 0 {
        return frexp_exponent(value * 18_446_744_073_709_551_616.0) - 64;
    }
    exponent - 1022
}

/// `ldexp(1.0, n)` for n in -1074..=1023.
fn ldexp_one(n: i32) -> f64 {
    if n >= -1022 {
        f64::from_bits(((n + 1023) as u64) << 52)
    } else {
        f64::from_bits(1u64 << (n + 1074))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn neumaier_sum() {
        assert_eq!(sum([1e16, 1.0, -1e16]), 1.0);
        assert_eq!(sum([0.1; 10]), 1.0);
        assert!(sum([-0.0]).is_sign_positive());
        assert!(sum([-0.0, -0.0]).is_sign_positive());
        assert_eq!(sum(std::iter::empty()), 0.0);
    }

    #[test]
    fn rounding_matches_cpython() {
        assert_eq!(round_digits(0.25, 1), 0.2);
        assert_eq!(round_digits(0.35, 1), 0.3);
        assert_eq!(round_digits(0.375, 2), 0.38);
        assert_eq!(round_digits(0.125, 2), 0.12);
        assert_eq!(round_digits(2.675, 2), 2.67);
        assert!(round_digits(-0.04, 1).is_sign_negative());
        assert_eq!(round_int(2.5).unwrap(), 2);
        assert_eq!(round_int(3.5).unwrap(), 4);
        assert_eq!(round_int(-0.5).unwrap(), 0);
        assert!(round_int(f64::NAN).is_err());
    }

    #[test]
    fn floor_division_and_modulo() {
        assert_eq!(floor_div(-1.0, 600.0), -1.0);
        assert_eq!(floor_div(1199.0, 600.0), 1.0);
        assert!(floor_div(-0.0, 600.0).is_sign_negative());
        assert_eq!(modulo(-0.25, 1.0), 0.75);
        assert!(modulo(-1.0, 1.0).is_sign_positive());
    }

    /// CPython results on random inputs, compared bit for bit. Runs when
    /// TRIAEVUM_FORGE_PYTHON names a Python 3.12+ interpreter.
    #[test]
    fn matches_cpython_bit_for_bit() {
        let Ok(python) = std::env::var("TRIAEVUM_FORGE_PYTHON") else {
            eprintln!("skipped: TRIAEVUM_FORGE_PYTHON is not set");
            return;
        };
        let manifest = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
        let directory = tempfile::tempdir().unwrap();
        let output = directory.path().join("numerics.json");
        let status = std::process::Command::new(python)
            .arg("-I")
            .arg(manifest.join("tests/oracle/asset_cases.py"))
            .arg(manifest.join("../../../.."))
            .arg(&output)
            .arg("numerics")
            .status()
            .unwrap();
        assert!(status.success());
        let cases: serde_json::Value = serde_json::from_slice(&std::fs::read(output).unwrap()).unwrap();
        let float = |value: &serde_json::Value| f64::from_bits(value.as_u64().unwrap());
        let floats = |value: &serde_json::Value| value.as_array().unwrap().iter().map(float).collect::<Vec<_>>();
        let same = |a: f64, b: f64| a.to_bits() == b.to_bits() || (a.is_nan() && b.is_nan());
        let mut failures = Vec::new();
        let mut check = |kind: &str, input: String, actual: f64, expected: f64| {
            if !same(actual, expected) {
                failures.push(format!("{kind}({input}) = {actual:e}, CPython {expected:e}"));
            }
        };
        for case in cases["sum"].as_array().unwrap() {
            let values = floats(&case[0]);
            check("sum", format!("{values:?}"), sum(values.iter().copied()), float(&case[1]));
        }
        for (kind, digits) in [("round1", 1), ("round3", 3)] {
            for case in cases[kind].as_array().unwrap() {
                let x = float(&case[0]);
                check(kind, format!("{x:e}"), round_digits(x, digits), float(&case[1]));
            }
        }
        for case in cases["round"].as_array().unwrap() {
            let x = float(&case[0]);
            check("round", format!("{x:e}"), round_int(x).unwrap() as f64, case[1].as_i64().unwrap() as f64);
        }
        for case in cases["hypot"].as_array().unwrap() {
            let values = floats(&case[0]);
            check("hypot", format!("{values:?}"), hypot(&values), float(&case[1]));
        }
        for case in cases["dist"].as_array().unwrap() {
            let values = floats(&case[0]);
            check("dist", format!("{values:?}"), dist(&values[..3], &values[3..]), float(&case[1]));
        }
        for case in cases["floordiv"].as_array().unwrap() {
            let x = float(&case[0]);
            check("floordiv", format!("{x:e}"), floor_div(x, 600.0), float(&case[1]));
        }
        for case in cases["mod"].as_array().unwrap() {
            let x = float(&case[0]);
            check("mod", format!("{x:e}"), modulo(x, 1.0), float(&case[1]));
        }
        for case in cases["cos"].as_array().unwrap() {
            let x = float(&case[0]);
            check("cos", format!("{x:e}"), cos(x).unwrap(), float(&case[1]));
        }
        for case in cases["sin"].as_array().unwrap() {
            let x = float(&case[0]);
            check("sin", format!("{x:e}"), sin(x).unwrap(), float(&case[1]));
        }
        for case in cases["atan2"].as_array().unwrap() {
            let values = floats(&case[0]);
            check("atan2", format!("{values:?}"), values[0].atan2(values[1]), float(&case[1]));
        }
        for case in cases["pow2"].as_array().unwrap() {
            let x = float(&case[0]);
            check("pow2", format!("{x:e}"), square_pow(x), float(&case[1]));
        }
        for case in cases["sqrt"].as_array().unwrap() {
            let x = float(&case[0]);
            check("sqrt", format!("{x:e}"), x.sqrt(), float(&case[1]));
        }
        assert!(
            failures.is_empty(),
            "{} differences:\n{}",
            failures.len(),
            failures[..failures.len().min(30)].join("\n")
        );
    }

    #[test]
    fn norms() {
        assert_eq!(hypot(&[3.0, 4.0]), 5.0);
        assert_eq!(dist(&[1.0, 1.0, 1.0], &[1.0, 1.0, 1.0]), 0.0);
        assert_eq!(hypot(&[-2.0]), 2.0);
        assert_eq!(ldexp_one(-1074), f64::from_bits(1));
        assert_eq!(frexp_exponent(1.0), 1);
        assert_eq!(frexp_exponent(0.5), 0);
        assert_eq!(frexp_exponent(f64::from_bits(1)), -1073);
    }
}
