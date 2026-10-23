#pragma once

#include "pica_affine_uniform_layout.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>

namespace Fast::Renderer3ds {
namespace PicaFactoredDetail {
using Vec = std::array<double, 3>;
using Rows = std::array<std::array<double, 4>, 3>;
inline double Dot(const Vec& a, const Vec& b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
inline Vec Cross(const Vec& a, const Vec& b) {
    return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}
inline Rows Product(const Rows& a, const Rows& b) {
    Rows out{};
    for (size_t i = 0; i < 3; ++i) for (size_t j = 0; j < 4; ++j) {
        out[i][j] = j == 3 ? a[i][3] : 0.0;
        for (size_t k = 0; k < 3; ++k) out[i][j] += a[i][k]*b[k][j];
    }
    return out;
}
inline Rows InverseRigid(const Rows& a) {
    Rows out{};
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            out[i][j] = a[j][i];
            out[i][3] -= a[j][i]*a[j][3];
        }
    }
    return out;
}
// References use the producer's R * positive diagonal scale convention.
// Residual matrices need only be finite, so shear/scale on another bone does
// not remove that bone from the shared group and break its joint constraint.
inline std::optional<Rows> RigidPart(const Rows& a) {
    Vec col[3];
    for (size_t j = 0; j < 3; ++j) {
        for (size_t i = 0; i < 3; ++i) {
            if (!std::isfinite(a[i][j]) || !std::isfinite(a[i][3])) return {};
            col[j][i] = a[i][j];
        }
        const double length = std::sqrt(Dot(col[j], col[j]));
        if (!(length > 1e-12)) return {};
        for (double& x : col[j]) x /= length;
    }
    constexpr double tolerance = 1e-3; // allow float24 rotation quantization
    if (std::abs(Dot(col[0],col[1])) > tolerance ||
        std::abs(Dot(col[0],col[2])) > tolerance ||
        std::abs(Dot(col[1],col[2])) > tolerance ||
        Dot(Cross(col[0],col[1]),col[2]) < 1.0-tolerance) return {};
    // Project the accepted reference to a proper rotation before using R^T
    // as its inverse. Original scale/rounding stays in the linear residual.
    const double dot = Dot(col[0],col[1]);
    for (size_t i = 0; i < 3; ++i) col[1][i] -= dot*col[0][i];
    const double length = std::sqrt(Dot(col[1],col[1]));
    for (double& x : col[1]) x /= length;
    col[2] = Cross(col[0],col[1]);
    Rows out{};
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 3; ++j) out[i][j] = col[j][i];
        out[i][3] = a[i][3];
    }
    return out;
}
// Principal SO(3) logarithm via a largest-component quaternion. Unlike
// division by sin(theta), this is well conditioned at a half turn.
inline Vec RotationLog(const Rows& r) {
    std::array<double, 4> q{};
    const double trace = r[0][0]+r[1][1]+r[2][2];
    if (trace > 0.0) {
        const double s = 2.0*std::sqrt(1.0+trace);
        q = {(r[2][1]-r[1][2])/s, (r[0][2]-r[2][0])/s, (r[1][0]-r[0][1])/s, s/4.0};
    } else {
        size_t i = 0;
        if (r[1][1] > r[i][i]) i = 1;
        if (r[2][2] > r[i][i]) i = 2;
        const size_t j = (i+1)%3, k = (i+2)%3;
        const double s = 2.0*std::sqrt(std::max(0.0, 1.0+r[i][i]-r[j][j]-r[k][k]));
        q[i] = s/4.0;
        q[j] = (r[j][i]+r[i][j])/s;
        q[k] = (r[k][i]+r[i][k])/s;
        q[3] = (r[k][j]-r[j][k])/s;
    }
    if (q[3] < 0.0) for (double& x : q) x = -x;
    const double length = std::sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]);
    const double factor = length > 1e-15 ? 2.0*std::atan2(length,q[3])/length : 2.0;
    return {q[0]*factor,q[1]*factor,q[2]*factor};
}
// Exp(alpha * Log(D)) in SE(3): translation uses the same twist as rotation.
// J^-1 and J use Taylor coefficients near zero (including pure translation).
inline Rows Screw(const Rows& d, double alpha) {
    const Vec omega = RotationLog(d), t{d[0][3],d[1][3],d[2][3]};
    const double theta2 = Dot(omega,omega), theta = std::sqrt(theta2);
    const double inverseJ = theta < 1e-4 ? 1.0/12.0+theta2/720.0+theta2*theta2/30240.0
        : (1.0-0.5*theta/std::tan(0.5*theta))/theta2;
    const Vec wt = Cross(omega,t), wwt = Cross(omega,wt);
    Vec v{};
    for (size_t i = 0; i < 3; ++i) v[i] = t[i]-0.5*wt[i]+inverseJ*wwt[i];
    const double a2 = alpha*alpha;
    const double b = theta < 1e-4 ? a2/2.0-a2*a2*theta2/24.0+a2*a2*a2*theta2*theta2/720.0
        : (1.0-std::cos(alpha*theta))/theta2;
    const double c = theta < 1e-4 ? alpha*a2/6.0-alpha*a2*a2*theta2/120.0+alpha*a2*a2*a2*theta2*theta2/5040.0
        : (alpha*theta-std::sin(alpha*theta))/(theta2*theta);
    const double sine = theta < 1e-4 ? alpha-alpha*a2*theta2/6.0+alpha*a2*a2*theta2*theta2/120.0
        : std::sin(alpha*theta)/theta;
    const Vec wv = Cross(omega,v), wwv = Cross(omega,wv);
    Rows out{};
    for (size_t j = 0; j < 3; ++j) {
        Vec unit{}; unit[j] = 1.0;
        const Vec w = Cross(omega,unit), ww = Cross(omega,w);
        for (size_t i = 0; i < 3; ++i) out[i][j] = unit[i]+sine*w[i]+b*ww[i];
    }
    for (size_t i = 0; i < 3; ++i) out[i][3] = alpha*v[i]+b*wv[i]+c*wwv[i];
    return out;
}
inline bool ValidFactoredGroup(std::span<const std::array<float, 4>> previous,
                               std::span<const std::array<float, 4>> current,
                               PicaAffineUniformGroup group, uint32_t selected) {
    return group.Valid() && group.MatrixCount <= 32U && selected &&
           (group.MatrixCount == 32U || !(selected >> group.MatrixCount)) &&
           previous.size() >= unsigned(group.FirstUniform)+3U*group.MatrixCount &&
           current.size() >= previous.size();
}
inline Rows GroupMatrix(std::span<const std::array<float, 4>> uniforms,
                        PicaAffineUniformGroup group, size_t slot) {
    Rows rows{};
    for (size_t i = 0; i < 3; ++i) for (size_t j = 0; j < 4; ++j)
        rows[i][j] = uniforms[group.FirstUniform+3*slot+i][j];
    return rows;
}
} // namespace PicaFactoredDetail

using PicaRigidMotionRows = PicaFactoredDetail::Rows;
using PicaAffineEndpoints = std::array<PicaFactoredDetail::Rows, 2>;

// The common left motion of a set of matrices that share an output space.
// Any common motion keeps shared joints (see InterpolatePicaFactoredUniformGroup),
// but the residual each matrix keeps is blended linearly: a reference that is
// a limb swinging against the body would carry the whole body along that
// limb's arc on the synthetic samples. The candidates are the matrices' own
// rigid motions D = T1 * inverse(T0); the one leaving the least residual
// sum ||inverse(D) N1 - N0|| is the motion of the body. A common whole-body
// motion leaves none and is found exactly. Ties keep the first candidate.
inline std::optional<PicaRigidMotionRows> PicaLeastResidualMotion(
    std::span<const PicaAffineEndpoints> matrices) {
    using namespace PicaFactoredDetail;
    std::optional<Rows> best;
    double bestResidual = std::numeric_limits<double>::infinity();
    for (const auto& candidate : matrices) {
        const auto a = RigidPart(candidate[0]), b = RigidPart(candidate[1]);
        if (!a || !b) continue;
        const Rows motion = Product(*b, InverseRigid(*a));
        const Rows inverse = InverseRigid(motion);
        double residual = 0.0;
        for (const auto& matrix : matrices) {
            const Rows carried = Product(inverse, matrix[1]);
            double squared = 0.0;
            for (size_t i = 0; i < 3; ++i) for (size_t j = 0; j < 4; ++j) {
                const double d = carried[i][j] - matrix[0][i][j];
                squared += d * d;
            }
            residual += std::sqrt(squared);
            if (!(residual < bestResidual)) break;
        }
        if (residual < bestResidual) {
            bestResidual = residual;
            best = motion;
        }
    }
    return best;
}

// The common left motion of one group's selected matrices.
inline std::optional<PicaRigidMotionRows> PicaFactoredReferenceMotion(
    std::span<const std::array<float, 4>> previous,
    std::span<const std::array<float, 4>> current,
    PicaAffineUniformGroup group, uint32_t selected) {
    using namespace PicaFactoredDetail;
    if (!ValidFactoredGroup(previous, current, group, selected)) return {};
    std::array<PicaAffineEndpoints, 32> matrices{};
    size_t count = 0;
    for (size_t slot = 0; slot < group.MatrixCount; ++slot) {
        if (!(selected & (uint32_t{1} << slot))) continue;
        matrices[count++] = {GroupMatrix(previous, group, slot), GroupMatrix(current, group, slot)};
    }
    return PicaLeastResidualMotion({matrices.data(), count});
}

// All selected matrices share one output space and one left motion. The
// caller supplies the proven span and referenced slots; no sliding scan.
// Matrices of several draws that share joints must also share the motion:
// the owner passes one sharedMotion to each of them. Without it the group's
// own reference is used. No partial writes on failure. Endpoints copy
// exactly, including signed zero.
inline bool InterpolatePicaFactoredUniformGroup(
    std::span<const std::array<float, 4>> previous,
    std::span<const std::array<float, 4>> current,
    PicaAffineUniformGroup group, uint32_t selected, float alpha,
    std::span<std::array<float, 4>> output,
    const PicaRigidMotionRows* sharedMotion = nullptr) {
    using namespace PicaFactoredDetail;
    if (!ValidFactoredGroup(previous, current, group, selected) || output.size() < previous.size() ||
        !std::isfinite(alpha) || alpha < 0.0F || alpha > 1.0F) return false;
    std::array<Rows, 32> before{}, after{};
    for (size_t slot = 0; slot < group.MatrixCount; ++slot) {
        if (!(selected & (uint32_t{1} << slot))) continue;
        before[slot] = GroupMatrix(previous, group, slot);
        after[slot] = GroupMatrix(current, group, slot);
        for (size_t i = 0; i < 3; ++i) for (size_t j = 0; j < 4; ++j)
            if (!std::isfinite(before[slot][i][j]) || !std::isfinite(after[slot][i][j])) return false;
    }
    std::optional<Rows> delta;
    if (sharedMotion) {
        for (const auto& row : *sharedMotion) for (double x : row) if (!std::isfinite(x)) return false;
        delta = *sharedMotion;
    } else {
        delta = PicaFactoredReferenceMotion(previous, current, group, selected);
    }
    if (!delta) return false;
    const auto inverse = InverseRigid(*delta), motion = Screw(*delta,alpha);
    for (size_t slot = 0; slot < group.MatrixCount; ++slot) {
        if (!(selected & (uint32_t{1} << slot))) continue;
        Rows residual = Product(inverse,after[slot]);
        for (size_t i = 0; i < 3; ++i) for (size_t j = 0; j < 4; ++j)
            residual[i][j] = (1.0-double(alpha))*before[slot][i][j]+double(alpha)*residual[i][j];
        after[slot] = Product(motion,residual);
        for (const auto& row : after[slot]) for (double x : row)
            if (!std::isfinite(x) || std::abs(x) > std::numeric_limits<float>::max()) return false;
    }
    for (size_t slot = 0; slot < group.MatrixCount; ++slot) {
        if (!(selected & (uint32_t{1} << slot))) continue;
        for (size_t i = 0; i < 3; ++i) for (size_t j = 0; j < 4; ++j) {
            const size_t row = group.FirstUniform+3*slot+i;
            output[row][j] = alpha == 0.0F ? previous[row][j] : alpha == 1.0F ? current[row][j]
                : static_cast<float>(after[slot][i][j]);
        }
    }
    return true;
}

} // namespace Fast::Renderer3ds
