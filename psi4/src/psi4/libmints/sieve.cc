/*
 * @BEGIN LICENSE
 *
 * Psi4: an open-source quantum chemistry software package
 *
 * Copyright (c) 2007-2022 The Psi4 Developers.
 *
 * The copyrights for code used from other parties are included in
 * the corresponding files.
 *
 * This file is part of Psi4.
 *
 * Psi4 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * Psi4 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along
 * with Psi4; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * @END LICENSE
 */

#include "psi4/libqt/qt.h"
#include "psi4/psi4-dec.h"
#include "psi4/libmints/sieve.h"
#include "psi4/libmints/basisset.h"
#include "psi4/libmints/twobody.h"
#include "psi4/libmints/integral.h"
#include "psi4/libpsi4util/PsiOutStream.h"
#include "psi4/libpsi4util/process.h"

#include <cfloat>

namespace psi {

ERISieve::ERISieve(std::shared_ptr<BasisSet> primary, double sieve, bool do_csam) : primary_(primary), sieve_(sieve), do_csam_(do_csam){
    common_init();
}

ERISieve::~ERISieve() {}

void ERISieve::common_init() {
    // if sieve_ is 0, then erfc_inv is infinite
    // the boost function just throws an error in this case
    // if (sieve_ > 0.0) {
    //    throw FeatureNotImplemented("libmints: sieve.cc", "replacement for boost::math::erfc_inv()", __FILE__,
    //    __LINE__); erfc_thresh_ = boost::math::erfc_inv(sieve_);
    //} else
    //    erfc_thresh_ = DBL_MAX;

    Options &options = Process::environment.options;
    do_qqr_ = false;  // Code below for QQR was/is utterly broken.

    debug_ = 0;

    integrals();
    if (do_csam_) csam_integrals();
    set_sieve(sieve_);
}

void ERISieve::set_sieve(double sieve) {
    sieve_ = sieve;
    sieve2_ = sieve_ * sieve;
    sieve_over_max_ = sieve_ / max_;
    sieve2_over_max_ = sieve2_ / max_;

    shell_pairs_.clear();
    function_pairs_.clear();
    shell_pairs_reverse_.resize(nshell_ * (nshell_ + 1L) / 2L);
    function_pairs_reverse_.resize(nbf_ * (nbf_ + 1L) / 2L);

    long int offset = 0L;
    size_t MUNU = 0L;
    for (int MU = 0; MU < nshell_; MU++) {
        for (int NU = 0; NU <= MU; NU++, MUNU++) {
            if (shell_pair_values_[MU * nshell_ + NU] >= sieve2_over_max_) {
                shell_pairs_.push_back(std::make_pair(MU, NU));
                shell_pairs_reverse_[MUNU] = offset;
                offset++;
            } else {
                shell_pairs_reverse_[MUNU] = -1L;
            }
        }
    }

    offset = 0L;
    size_t munu = 0L;
    for (int mu = 0; mu < nbf_; mu++) {
        for (int nu = 0; nu <= mu; nu++, munu++) {
            if (function_pair_values_[mu * nbf_ + nu] >= sieve2_over_max_) {
                function_pairs_.push_back(std::make_pair(mu, nu));
                function_pairs_reverse_[munu] = offset;
                offset++;
            } else {
                function_pairs_reverse_[munu] = -1L;
            }
        }
    }

    shell_to_shell_.clear();
    function_to_function_.clear();
    shell_to_shell_.resize(nshell_);
    function_to_function_.resize(nbf_);

    for (int MU = 0; MU < nshell_; MU++) {
        for (int NU = 0; NU < nshell_; NU++) {
            if (shell_pair_values_[MU * nshell_ + NU] >= sieve2_over_max_) {
                shell_to_shell_[MU].push_back(NU);
            }
        }
    }

    for (int mu = 0; mu < nbf_; mu++) {
        for (int nu = 0; nu < nbf_; nu++) {
            if (function_pair_values_[mu * nbf_ + nu] >= sieve2_over_max_) {
                function_to_function_[mu].push_back(nu);
            }
        }
    }

    if (debug_) {
        outfile->Printf("  ==> ERISieve Debug <==\n\n");
        outfile->Printf("    Sieve Cutoff = %11.3E\n", sieve_);
        outfile->Printf("    Sieve^2      = %11.3E\n", sieve2_);
        outfile->Printf("    Max          = %11.3E\n", max_);
        outfile->Printf("    Sieve/Max    = %11.3E\n", sieve_over_max_);
        outfile->Printf("    Sieve^2/Max  = %11.3E\n\n", sieve2_over_max_);

        primary_->print_by_level("outfile", 3);

        outfile->Printf("   => Shell Pair Values <=\n\n");
        for (int M = 0; M < nshell_; M++) {
            for (int N = 0; N < nshell_; N++) {
                outfile->Printf("    (%3d, %3d| = %11.3E\n", M, N, shell_pair_values_[M * nshell_ + N]);
            }
        }
        outfile->Printf("\n");

        outfile->Printf("   => Function Pair Values <=\n\n");
        for (int M = 0; M < nbf_; M++) {
            for (int N = 0; N < nbf_; N++) {
                outfile->Printf("    (%3d, %3d| = %11.3E\n", M, N, function_pair_values_[M * nbf_ + N]);
            }
        }
        outfile->Printf("\n");

        outfile->Printf("   => Significant Shell Pairs <=\n\n");
        for (int MN = 0; MN < (int)shell_pairs_.size(); MN++) {
            outfile->Printf("    %6d = (%3d,%3d|\n", MN, shell_pairs_[MN].first, shell_pairs_[MN].second);
        }
        outfile->Printf("\n");

        outfile->Printf("   => Significant Function Pairs <=\n\n");
        for (int MN = 0; MN < (int)function_pairs_.size(); MN++) {
            outfile->Printf("    %6d = (%3d,%3d|\n", MN, function_pairs_[MN].first, function_pairs_[MN].second);
        }
        outfile->Printf("\n");

        outfile->Printf("   => Significant Shell Pairs Reverse <=\n\n");
        for (int M = 0; M < nshell_; M++) {
            for (int N = 0; N <= M; N++) {
                outfile->Printf("    %6ld = (%3d,%3d|\n", shell_pairs_reverse_[M * (M + 1) / 2 + N], M, N);
            }
        }
        outfile->Printf("\n");

        outfile->Printf("   => Significant Function Pairs Reverse <=\n\n");
        for (int M = 0; M < nbf_; M++) {
            for (int N = 0; N <= M; N++) {
                outfile->Printf("    %6ld = (%3d,%3d|\n", function_pairs_reverse_[M * (M + 1) / 2 + N], M, N);
            }
        }
        outfile->Printf("\n");

        outfile->Printf("   => Shell to Shell <=\n\n");
        for (int M = 0; M < nshell_; M++) {
            for (int N = 0; N < (int)shell_to_shell_[M].size(); N++) {
                outfile->Printf("    (%3d, %3d|\n", M, shell_to_shell_[M][N]);
            }
        }
        outfile->Printf("\n");

        outfile->Printf("   => Function to Function <=\n\n");
        for (int M = 0; M < nbf_; M++) {
            for (int N = 0; N < (int)function_to_function_[M].size(); N++) {
                outfile->Printf("    (%3d, %3d|\n", M, function_to_function_[M][N]);
            }
        }
        outfile->Printf("\n");
    }
}

void ERISieve::integrals() {
    size_t nshell = primary_->nshell();
    size_t nbf = primary_->nbf();

    nbf_ = nbf;
    nshell_ = nshell;

    function_pair_values_.resize(nbf * nbf);
    shell_pair_values_.resize(nshell * nshell);
    ::memset(&function_pair_values_[0], '\0', sizeof(double) * nbf * nbf);
    ::memset(&shell_pair_values_[0], '\0', sizeof(double) * nshell * nshell);
    max_ = 0.0;

    IntegralFactory schwarzfactory(primary_, primary_, primary_, primary_);
    std::shared_ptr<TwoBodyAOInt> eri = std::shared_ptr<TwoBodyAOInt>(schwarzfactory.eri());

    for (int P = 0; P < nshell_; P++) {
        for (int Q = 0; Q <= P; Q++) {
            int nP = primary_->shell(P).nfunction();
            int nQ = primary_->shell(Q).nfunction();
            int oP = primary_->shell(P).function_index();
            int oQ = primary_->shell(Q).function_index();
            eri->compute_shell(P, Q, P, Q);
            const double *buffer = eri->buffer();
            double max_val = 0.0;
            for (int p = 0; p < nP; p++) {
                for (int q = 0; q < nQ; q++) {
                    max_val = std::max(max_val, std::abs(buffer[p * (nQ * nP * nQ + nQ) + q * (nP * nQ + 1)]));
                }
            }
            max_ = std::max(max_, max_val);
            shell_pair_values_[P * nshell_ + Q] = shell_pair_values_[Q * nshell_ + P] = max_val;
            for (int p = 0; p < nP; p++) {
                for (int q = 0; q < nQ; q++) {
                    function_pair_values_[(p + oP) * nbf_ + (q + oQ)] =
                        function_pair_values_[(q + oQ) * nbf_ + (p + oP)] = max_val;
                }
            }
        }
    }

// All this is broken (only built one shell-pair's info)
#if 0
    if (do_qqr_) {

            //std::cout << "Doing QQR precomputations.\n";

            const GaussianShell& mu_shell = primary_->shell(MU);
            const GaussianShell& nu_shell = primary_->shell(NU);

            double coef_denominator = 0.0;

            std::vector<Vector3> primitive_centers;
            std::vector<double> primitive_extents;

            // IMPORTANT: need to compute this first, outside these loops
            Vector3 contracted_center(0.0, 0.0, 0.0);

            for (int p_ind = 0; p_ind < mu_shell.nprimitive(); p_ind++)
            {

              double p_exp = mu_shell.exp(p_ind);
              double p_coef = std::fabs(mu_shell.coef(p_ind));

              Vector3 p_center = mu_shell.center();
              p_center *= p_exp;

              for (int r_ind = 0; r_ind < nu_shell.nprimitive(); r_ind++)
              {

                double r_exp = nu_shell.exp(r_ind);
                double r_coef = std::fabs(nu_shell.coef(r_ind));

                Vector3 r_center = nu_shell.center();
                r_center *= r_exp;

                Vector3 primitive_center = (p_center + r_center) / (p_exp + r_exp);

                primitive_centers.push_back(primitive_center);

                coef_denominator += p_coef * r_coef;
                contracted_center += primitive_center;

                double this_extent = sqrt(2 / (p_exp + r_exp)) * erfc_thresh_;

                primitive_extents.push_back(this_extent);

              } // loop over primitives in NU

            } // loop over primitives in MU

            contracted_center /= coef_denominator;
            size_t this_ind = MU * (size_t) nshell + NU;
            size_t sym_ind = NU * (size_t) nshell + MU;

            for (int prim_it = 0; prim_it < (int)primitive_extents.size(); prim_it++)
            {

              double this_dist = contracted_center.distance(primitive_centers[prim_it]);

              extents_[this_ind] = std::max(extents_[this_ind],
                                            primitive_extents[prim_it]
                                              + this_dist);
              extents_[sym_ind] = extents_[this_ind];

            } // now loop over the pairs of primitives


     } // doing QQR
#endif
}

void ERISieve::csam_integrals() {
    function_sqrt_.resize(nbf_);
    shell_pair_exchange_values_.resize(nshell_ * nshell_);
    std::fill(function_sqrt_.begin(), function_sqrt_.end(), 0.0);
    std::fill(shell_pair_exchange_values_.begin(), shell_pair_exchange_values_.end(), 0.0);

    IntegralFactory csamfactory(primary_, primary_, primary_, primary_);
    std::shared_ptr<TwoBodyAOInt> eri = std::shared_ptr<TwoBodyAOInt>(csamfactory.eri());
    const double *buffer = eri->buffer();

    for (int P = 0; P < nshell_; P++) {
        for (int Q = P; Q >= 0; Q--) {
            int nP = primary_->shell(P).nfunction();
            int nQ = primary_->shell(Q).nfunction();
            int oP = primary_->shell(P).function_index();
            int oQ = primary_->shell(Q).function_index();
            eri->compute_shell(P, P, Q, Q);

            // Computing Q_mu_mu (for denominator of Eq.9)
            if (Q == P) {
                int oP = primary_->shell(P).function_index();
                for (int p = 0; p < nP; ++p) {
                    function_sqrt_[oP + p] = std::sqrt(std::abs(buffer[p * (nP * nP * nP + nP) + p * (nP * nP + 1)]));
                }
            }

            // Computing square of M~_mu_lam (defined in Eq. 9)
            double max_val = 0.0;
            for (int p = 0; p < nP; p++) {
                for (int q = 0; q < nQ; q++) {
                    max_val = std::max(max_val, std::abs(buffer[p * nQ * nQ * (nP + 1) + q * (nQ + 1)]) /
                                                    (function_sqrt_[p + oP] * function_sqrt_[q + oQ]));
                }
            }
            shell_pair_exchange_values_[P * nshell_ + Q] = shell_pair_exchange_values_[Q * nshell_ + P] = max_val;
        }
    }
}

bool ERISieve::shell_significant_qqr(int M, int N, int R, int S) {
    double Q_mn = shell_pair_values_[N * nshell_ + M];
    double Q_rs = shell_pair_values_[R * nshell_ + S];

    double dist = contracted_centers_[N * nshell_ + M].distance(contracted_centers_[R * nshell_ + S]);

    double denom = dist - extents_[N * nshell_ + M] - extents_[R * nshell_ + S];

    // this does the near field estimate if that's the only valid one
    // values of Q are squared
    double est = Q_mn * Q_rs / (denom > 0.0 ? denom * denom : 1.0);
    if (denom > 0.0) {
        std::cout << "Q_mn: " << Q_mn << ", ";
        std::cout << "Q_rs: " << Q_rs << ", ";
        std::cout << "dist: " << dist << ", ";
        std::cout << "denom: " << denom << ", ";
        std::cout << "est: " << est << ", ";
        std::cout << "sieve2: " << sieve2_ << "\n";
    }
    return est >= sieve2_;
}

bool ERISieve::shell_significant_csam(int M, int N, int R, int S) {
    // Square of standard Cauchy-Schwarz Q_mu_nu terms (Eq. 1)
    double mn_mn = shell_pair_values_[N * nshell_ + M];
    double rs_rs = shell_pair_values_[S * nshell_ + R];

    // Square of M~_mu_nu terms (Eq. 9)
    double mm_rr = shell_pair_exchange_values_[R * nshell_ + M];
    double nn_ss = shell_pair_exchange_values_[S * nshell_ + N];
    double mm_ss = shell_pair_exchange_values_[S * nshell_ + M];
    double nn_rr = shell_pair_exchange_values_[R * nshell_ + N];

    // Square of M_mu_nu_lam_sig (Eq. 12)
    double csam_2 = std::max(mm_rr * nn_ss, mm_ss * nn_rr);

    // Square of Eq. 11
    double mnrs_2 = mn_mn * rs_rs * csam_2;

    return std::abs(mnrs_2) >= sieve2_;
}

double ERISieve::shell_pair_value(int m, int n) const { return shell_pair_values_[m * nshell_ + n]; }
}  // namespace psi
