//
// Created by Lars Gebraad on 25.01.19.
//

#include <omp.h>
#include <iostream>
#include <cmath>
#include <fstream>
#include <limits>
#include <iomanip>
#include "fdWaveModel.h"

#define PI 3.14159265

fdWaveModel::fdWaveModel() {
    // --- Initialization section ---

    // Allocate fields
    allocate_2d_array(vx, nx, nz);
    allocate_2d_array(vz, nx, nz);
    allocate_2d_array(txx, nx, nz);
    allocate_2d_array(tzz, nx, nz);
    allocate_2d_array(txz, nx, nz);

    allocate_2d_array(lm, nx, nz);
    allocate_2d_array(la, nx, nz);
    allocate_2d_array(mu, nx, nz);
    allocate_2d_array(b_vx, nx, nz);
    allocate_2d_array(b_vz, nx, nz);
    allocate_2d_array(rho, nx, nz);
    allocate_2d_array(vp, nx, nz);
    allocate_2d_array(vs, nx, nz);

    allocate_2d_array(density_l_kernel, nx, nz);
    allocate_2d_array(lambda_kernel, nx, nz);
    allocate_2d_array(mu_kernel, nx, nz);

    allocate_2d_array(vp_kernel, nx, nz);
    allocate_2d_array(vs_kernel, nx, nz);
    allocate_2d_array(density_v_kernel, nx, nz);

    allocate_2d_array(starting_rho, nx, nz);
    allocate_2d_array(starting_vp, nx, nz);
    allocate_2d_array(starting_vs, nx, nz);

    allocate_2d_array(taper, nx, nz);

    //
    allocate_1d_array(t, nt);
    allocate_2d_array(stf, n_sources, nt);
    allocate_3d_array(moment, n_sources, 2, 2);
    allocate_3d_array(rtf_ux, n_shots, nr, nt);
    allocate_3d_array(rtf_uz, n_shots, nr, nt);
    allocate_3d_array(rtf_ux_true, n_shots, nr, nt);
    allocate_3d_array(rtf_uz_true, n_shots, nr, nt);
    allocate_3d_array(a_stf_ux, n_shots, nr, nt);
    allocate_3d_array(a_stf_uz, n_shots, nr, nt);
    allocate_4d_array(accu_vx, n_shots, snapshots, nx, nz);
    allocate_4d_array(accu_vz, n_shots, snapshots, nx, nz);
    allocate_4d_array(accu_txx, n_shots, snapshots, nx, nz);
    allocate_4d_array(accu_tzz, n_shots, snapshots, nx, nz);
    allocate_4d_array(accu_txz, n_shots, snapshots, nx, nz);

    // Place sources/receivers inside the domain
    if (add_np_to_receiver_location) {
        for (int ir = 0; ir < nr; ++ir) {
            ix_receivers[ir] += np_boundary;
            iz_receivers[ir] += np_boundary;
        }
    }
    if (add_np_to_source_location) {
        for (int is = 0; is < n_sources; ++is) {
            ix_sources[is] += np_boundary;
            iz_sources[is] += np_boundary;
        }
    }

    // Initialize data variance to one (should for now be taken care of it outside of the code)
//    std::fill(&data_variance_ux[0][0][0], &data_variance_ux[0][0][0] + sizeof(data_variance_ux) / sizeof(real_simulation), 1);
//    std::fill(&data_variance_uz[0][0][0], &data_variance_uz[0][0][0] + sizeof(data_variance_uz) / sizeof(real_simulation), 1);

    // Assign stf/rtf_ux
    for (int i_shot = 0; i_shot < n_shots; ++i_shot) {
        for (int i_source = 0; i_source < which_source_to_fire_in_which_shot[i_shot].size(); ++i_source) {
            for (unsigned int it = 0; it < nt; ++it) {
                t[it] = it * dt;
                auto f = static_cast<real_simulation>(1.0 / alpha);
                auto shiftedTime = static_cast<real_simulation>(t[it] - 1.4 / f - delay_per_shot * i_source / f);
                stf[which_source_to_fire_in_which_shot[i_shot][i_source]][it] = real_simulation(
                        (1 - 2 * pow(M_PI * f * shiftedTime, 2)) * exp(-pow(M_PI * f * shiftedTime, 2)));
            }
        }
    }

    for (int i_source = 0; i_source < n_sources; ++i_source) {
        moment[i_source][0][0] = static_cast<real_simulation>(cos(moment_angles[i_source] * PI / 180.0) * 1e15);
        moment[i_source][0][1] = static_cast<real_simulation>(-sin(moment_angles[i_source] * PI / 180.0) * 1e15);
        moment[i_source][1][0] = static_cast<real_simulation>(-sin(moment_angles[i_source] * PI / 180.0) * 1e15);
        moment[i_source][1][1] = static_cast<real_simulation>(-cos(moment_angles[i_source] * PI / 180.0) * 1e15);
    }

    // Setting all fields.
    std::fill(*vp, &vp[nx - 1][nz - 1] + 1, scalar_vp);
    std::fill(*vs, &vs[nx - 1][nz - 1] + 1, scalar_vs);
    std::fill(*rho, &rho[nx - 1][nz - 1] + 1, scalar_rho);

    update_from_velocity();

    {
        // Initialize
        std::fill(*taper, &taper[nx - 1][nz - 1] + 1, 0.0);
        for (int id = 0; id < np_boundary; ++id) {
            for (int ix = id; ix < nx - id; ++ix) {
                for (int iz = id; iz < nz; ++iz) {
                    taper[ix][iz]++;
                }
            }
        }

        for (int ix = 0; ix < nx; ++ix) {
            for (int iz = 0; iz < nz; ++iz) {
                taper[ix][iz] = static_cast<real_simulation>(exp(-pow(np_factor * (np_boundary - taper[ix][iz]), 2)));
            }
        }
    }

    if (floor(double(nt) / snapshot_interval) != snapshots) {
        throw std::length_error("Snapshot interval and size of accumulator don't match!");
    }
}

fdWaveModel::~fdWaveModel() {
    deallocate_2d_array(vx, nx);
    deallocate_2d_array(vz, nx);
    deallocate_2d_array(txx, nx);
    deallocate_2d_array(tzz, nx);
    deallocate_2d_array(txz, nx);

    deallocate_2d_array(lm, nx);
    deallocate_2d_array(la, nx);
    deallocate_2d_array(mu, nx);
    deallocate_2d_array(b_vx, nx);
    deallocate_2d_array(b_vz, nx);
    deallocate_2d_array(rho, nx);
    deallocate_2d_array(vp, nx);
    deallocate_2d_array(vs, nx);

    deallocate_2d_array(density_l_kernel, nx);
    deallocate_2d_array(lambda_kernel, nx);
    deallocate_2d_array(mu_kernel, nx);

    deallocate_2d_array(vp_kernel, nx);
    deallocate_2d_array(vs_kernel, nx);
    deallocate_2d_array(density_v_kernel, nx);

    deallocate_2d_array(starting_rho, nx);
    deallocate_2d_array(starting_vp, nx);
    deallocate_2d_array(starting_vs, nx);

    deallocate_2d_array(taper, nx);

    deallocate_1d_array(t);
    deallocate_2d_array(stf, n_sources);
    deallocate_3d_array(moment, n_sources, 2);
    deallocate_3d_array(rtf_ux, n_shots, nr);
    deallocate_3d_array(rtf_uz, n_shots, nr);
    deallocate_3d_array(rtf_ux_true, n_shots, nr);
    deallocate_3d_array(rtf_uz_true, n_shots, nr);
    deallocate_3d_array(a_stf_ux, n_shots, nr);
    deallocate_3d_array(a_stf_uz, n_shots, nr);
    deallocate_4d_array(accu_vx, n_shots, snapshots, nx);
    deallocate_4d_array(accu_vz, n_shots, snapshots, nx);
    deallocate_4d_array(accu_txx, n_shots, snapshots, nx);
    deallocate_4d_array(accu_tzz, n_shots, snapshots, nx);
    deallocate_4d_array(accu_txz, n_shots, snapshots, nx);
}

// Forward modeller
void fdWaveModel::forward_simulate(int i_shot, bool store_fields, bool verbose) {

    std::fill(*vx, &vx[nx - 1][nz - 1] + 1, 0.0);
    std::fill(*vz, &vz[nx - 1][nz - 1] + 1, 0.0);
    std::fill(*txx, &txx[nx - 1][nz - 1] + 1, 0.0);
    std::fill(*tzz, &tzz[nx - 1][nz - 1] + 1, 0.0);
    std::fill(*txz, &txz[nx - 1][nz - 1] + 1, 0.0);

    // If verbose, count time
    double startTime = 0, stopTime = 0, secsElapsed = 0;
    if (verbose) { startTime = real_simulation(omp_get_wtime()); }

    for (int it = 0; it < nt; ++it) {
        // Take wavefield snapshot
        if (it % snapshot_interval == 0 and store_fields) {
            #pragma omp parallel for collapse(2)
            for (int ix = np_boundary; ix < nx_inner + np_boundary; ++ix) {
                for (int iz = np_boundary; iz < nz_inner + np_boundary; ++iz) {
                    accu_vx[i_shot][it / snapshot_interval][ix][iz] = vx[ix][iz];
                    accu_vz[i_shot][it / snapshot_interval][ix][iz] = vz[ix][iz];
                    accu_txx[i_shot][it / snapshot_interval][ix][iz] = txx[ix][iz];
                    accu_txz[i_shot][it / snapshot_interval][ix][iz] = txz[ix][iz];
                    accu_tzz[i_shot][it / snapshot_interval][ix][iz] = tzz[ix][iz];
                }
            }
        }

        // Record seismograms by integrating velocity into displacement
        #pragma omp parallel for collapse(1)
        for (int i_receiver = 0; i_receiver < nr; ++i_receiver) {
            if (it == 0) {
                rtf_ux[i_shot][i_receiver][it] = dt * vx[ix_receivers[i_receiver]][iz_receivers[i_receiver]] / (dx * dz);
                rtf_uz[i_shot][i_receiver][it] = dt * vz[ix_receivers[i_receiver]][iz_receivers[i_receiver]] / (dx * dz);
            } else {
                rtf_ux[i_shot][i_receiver][it] =
                        rtf_ux[i_shot][i_receiver][it - 1] + dt * vx[ix_receivers[i_receiver]][iz_receivers[i_receiver]] / (dx * dz);
                rtf_uz[i_shot][i_receiver][it] =
                        rtf_uz[i_shot][i_receiver][it - 1] + dt * vz[ix_receivers[i_receiver]][iz_receivers[i_receiver]] / (dx * dz);
            }
        }

        // Time integrate dynamic fields for stress
        #pragma omp parallel for collapse(2)
        for (int ix = 2; ix < nx - 2; ++ix) {
            for (int iz = 2; iz < nz - 2; ++iz) {
                txx[ix][iz] = taper[ix][iz] *
                              (txx[ix][iz] +
                               dt *
                               (lm[ix][iz] * (
                                       c1 * (vx[ix + 1][iz] - vx[ix][iz]) +
                                       c2 * (vx[ix - 1][iz] - vx[ix + 2][iz])) / dx +
                                la[ix][iz] * (
                                        c1 * (vz[ix][iz] - vz[ix][iz - 1]) +
                                        c2 * (vz[ix][iz - 2] - vz[ix][iz + 1])) / dz));
                tzz[ix][iz] = taper[ix][iz] *
                              (tzz[ix][iz] +
                               dt *
                               (la[ix][iz] * (
                                       c1 * (vx[ix + 1][iz] - vx[ix][iz]) +
                                       c2 * (vx[ix - 1][iz] - vx[ix + 2][iz])) / dx +
                                (lm[ix][iz]) * (
                                        c1 * (vz[ix][iz] - vz[ix][iz - 1]) +
                                        c2 * (vz[ix][iz - 2] - vz[ix][iz + 1])) / dz));
                txz[ix][iz] = taper[ix][iz] *
                              (txz[ix][iz] + dt * mu[ix][iz] * (
                                      (c1 * (vx[ix][iz + 1] - vx[ix][iz]) +
                                       c2 * (vx[ix][iz - 1] - vx[ix][iz + 2])) / dz +
                                      (c1 * (vz[ix][iz] - vz[ix - 1][iz]) +
                                       c2 * (vz[ix - 2][iz] - vz[ix + 1][iz])) / dx));

            }
        }
        // Time integrate dynamic fields for velocity
        #pragma omp parallel for collapse(2)
        for (int ix = 2; ix < nx - 2; ++ix) {
            for (int iz = 2; iz < nz - 2; ++iz) {
                vx[ix][iz] =
                        taper[ix][iz] *
                        (vx[ix][iz]
                         + b_vx[ix][iz] * dt * (
                                (c1 * (txx[ix][iz] - txx[ix - 1][iz]) +
                                 c2 * (txx[ix - 2][iz] - txx[ix + 1][iz])) / dx +
                                (c1 * (txz[ix][iz] - txz[ix][iz - 1]) +
                                 c2 * (txz[ix][iz - 2] - txz[ix][iz + 1])) / dz));
                vz[ix][iz] =
                        taper[ix][iz] *
                        (vz[ix][iz]
                         + b_vz[ix][iz] * dt * (
                                (c1 * (txz[ix + 1][iz] - txz[ix][iz]) +
                                 c2 * (txz[ix - 1][iz] - txz[ix + 2][iz])) / dx +
                                (c1 * (tzz[ix][iz + 1] - tzz[ix][iz]) +
                                 c2 * (tzz[ix][iz - 1] - tzz[ix][iz + 2])) / dz));

            }
        }

        for (const auto &i_source : which_source_to_fire_in_which_shot[i_shot]) {
            if (it < 1 and verbose) { std::cout << "Firing source " << i_source << " in shot " << i_shot << std::endl; }
            // |-inject source
            // | (x,x)-couple
            vx[ix_sources[i_source] - 1][iz_sources[i_source]] -=
                    moment[i_source][0][0] * stf[i_source][it] * dt *
                    b_vz[ix_sources[i_source] - 1][iz_sources[i_source]] /
                    (dx * dx * dx * dx);
            vx[ix_sources[i_source]][iz_sources[i_source]] +=
                    moment[i_source][0][0] * stf[i_source][it] * dt *
                    b_vz[ix_sources[i_source]][iz_sources[i_source]] /
                    (dx * dx * dx * dx);
            // | (z,z)-couple
            vz[ix_sources[i_source]][iz_sources[i_source] - 1] -=
                    moment[i_source][1][1] * stf[i_source][it] * dt *
                    b_vz[ix_sources[i_source]][iz_sources[i_source] - 1] /
                    (dz * dz * dz * dz);
            vz[ix_sources[i_source]][iz_sources[i_source]] +=
                    moment[i_source][1][1] * stf[i_source][it] * dt *
                    b_vz[ix_sources[i_source]][iz_sources[i_source]] /
                    (dz * dz * dz * dz);
            // | (x,z)-couple
            vx[ix_sources[i_source] - 1][iz_sources[i_source] + 1] +=
                    0.25 * moment[i_source][0][1] * stf[i_source][it] * dt *
                    b_vz[ix_sources[i_source] - 1][iz_sources[i_source] + 1] /
                    (dx * dx * dx * dx);
            vx[ix_sources[i_source]][iz_sources[i_source] + 1] +=
                    0.25 * moment[i_source][0][1] * stf[i_source][it] * dt *
                    b_vz[ix_sources[i_source]][iz_sources[i_source] + 1] /
                    (dx * dx * dx * dx);
            vx[ix_sources[i_source] - 1][iz_sources[i_source] - 1] -=
                    0.25 * moment[i_source][0][1] * stf[i_source][it] * dt *
                    b_vz[ix_sources[i_source] - 1][iz_sources[i_source] - 1] /
                    (dx * dx * dx * dx);
            vx[ix_sources[i_source]][iz_sources[i_source] - 1] -=
                    0.25 * moment[i_source][0][1] * stf[i_source][it] * dt *
                    b_vz[ix_sources[i_source]][iz_sources[i_source] - 1] /
                    (dx * dx * dx * dx);
            // | (z,x)-couple
            vz[ix_sources[i_source] + 1][iz_sources[i_source] - 1] +=
                    0.25 * moment[i_source][1][0] * stf[i_source][it] * dt *
                    b_vz[ix_sources[i_source] + 1][iz_sources[i_source] - 1] /
                    (dz * dz * dz * dz);
            vz[ix_sources[i_source] + 1][iz_sources[i_source]] +=
                    0.25 * moment[i_source][1][0] * stf[i_source][it] * dt *
                    b_vz[ix_sources[i_source] + 1][iz_sources[i_source]] /
                    (dz * dz * dz * dz);
            vz[ix_sources[i_source] - 1][iz_sources[i_source] - 1] -=
                    0.25 * moment[i_source][1][0] * stf[i_source][it] * dt *
                    b_vz[ix_sources[i_source] - 1][iz_sources[i_source] - 1] /
                    (dz * dz * dz * dz);
            vz[ix_sources[i_source] - 1][iz_sources[i_source]] -=
                    0.25 * moment[i_source][1][0] * stf[i_source][it] * dt *
                    b_vz[ix_sources[i_source] - 1][iz_sources[i_source]] /
                    (dz * dz * dz * dz);
        }
    }

    // Output timing
    if (verbose) {
        stopTime = omp_get_wtime();
        secsElapsed = stopTime - startTime;
        std::cout << "Seconds elapsed for forward wave simulation: " << secsElapsed << std::endl;
    }
}

void fdWaveModel::adjoint_simulate(int i_shot, bool verbose) {
    // Reset dynamical fields
    std::fill(*vx, &vx[nx - 1][nz - 1] + 1, 0.0);
    std::fill(*vz, &vz[nx - 1][nz - 1] + 1, 0.0);
    std::fill(*txx, &txx[nx - 1][nz - 1] + 1, 0.0);
    std::fill(*tzz, &tzz[nx - 1][nz - 1] + 1, 0.0);
    std::fill(*txz, &txz[nx - 1][nz - 1] + 1, 0.0);

    // If verbose, count time
    double startTime = 0, stopTime = 0, secsElapsed = 0;
    if (verbose) { startTime = real_simulation(omp_get_wtime()); }

    for (int it = nt - 1; it >= 0; --it) {

        // Correlate wavefields
        if (it % snapshot_interval == 0) { // Todo, [X] rewrite for only relevant parameters [ ] Check if done properly
            #pragma omp parallel for collapse(2)
            for (int ix = np_boundary + nx_inner_boundary; ix < np_boundary + nx_inner - nx_inner_boundary; ++ix) {
                for (int iz = np_boundary + nz_inner_boundary; iz < np_boundary + nz_inner - nz_inner_boundary; ++iz) {
                    density_l_kernel[ix][iz] -= snapshot_interval * dt * (accu_vx[i_shot][it / snapshot_interval][ix][iz] * vx[ix][iz] +
                                                                          accu_vz[i_shot][it / snapshot_interval][ix][iz] * vz[ix][iz]);

                    lambda_kernel[ix][iz] += snapshot_interval * dt *
                                             (((accu_txx[i_shot][it / snapshot_interval][ix][iz] -
                                                (accu_tzz[i_shot][it / snapshot_interval][ix][iz] * la[ix][iz]) / lm[ix][iz]) +
                                               (accu_tzz[i_shot][it / snapshot_interval][ix][iz] -
                                                (accu_txx[i_shot][it / snapshot_interval][ix][iz] * la[ix][iz]) / lm[ix][iz]))
                                              * ((txx[ix][iz] - (tzz[ix][iz] * la[ix][iz]) / lm[ix][iz]) +
                                                 (tzz[ix][iz] - (txx[ix][iz] * la[ix][iz]) / lm[ix][iz]))) /
                                             ((lm[ix][iz] - ((la[ix][iz] * la[ix][iz]) / (lm[ix][iz]))) *
                                              (lm[ix][iz] - ((la[ix][iz] * la[ix][iz]) / (lm[ix][iz]))));

                    mu_kernel[ix][iz] += snapshot_interval * dt * 2 *
                                         ((((txx[ix][iz] - (tzz[ix][iz] * la[ix][iz]) / lm[ix][iz]) *
                                            (accu_txx[i_shot][it / snapshot_interval][ix][iz] -
                                             (accu_tzz[i_shot][it / snapshot_interval][ix][iz] * la[ix][iz]) /
                                             lm[ix][iz])) +
                                           ((tzz[ix][iz] - (txx[ix][iz] * la[ix][iz]) / lm[ix][iz]) *
                                            (accu_tzz[i_shot][it / snapshot_interval][ix][iz] -
                                             (accu_txx[i_shot][it / snapshot_interval][ix][iz] * la[ix][iz]) /
                                             lm[ix][iz]))
                                          ) / ((lm[ix][iz] - ((la[ix][iz] * la[ix][iz]) / (lm[ix][iz]))) *
                                               (lm[ix][iz] - ((la[ix][iz] * la[ix][iz]) / (lm[ix][iz])))) +
                                          2 * (txz[ix][iz] * accu_txz[i_shot][it / snapshot_interval][ix][iz] / (4 * mu[ix][iz] * mu[ix][iz])));
                }
            }
        }

        // Reverse time integrate dynamic fields for stress
        #pragma omp parallel for collapse(2)
        for (int ix = 2; ix < nx - 2; ++ix) {
            for (int iz = 2; iz < nz - 2; ++iz) {
                txx[ix][iz] = taper[ix][iz] *
                              (txx[ix][iz] -
                               dt *
                               (lm[ix][iz] * (
                                       c1 * (vx[ix + 1][iz] - vx[ix][iz]) +
                                       c2 * (vx[ix - 1][iz] - vx[ix + 2][iz])) / dx +
                                la[ix][iz] * (
                                        c1 * (vz[ix][iz] - vz[ix][iz - 1]) +
                                        c2 * (vz[ix][iz - 2] - vz[ix][iz + 1])) / dz));
                tzz[ix][iz] = taper[ix][iz] *
                              (tzz[ix][iz] -
                               dt *
                               (la[ix][iz] * (
                                       c1 * (vx[ix + 1][iz] - vx[ix][iz]) +
                                       c2 * (vx[ix - 1][iz] - vx[ix + 2][iz])) / dx +
                                (lm[ix][iz]) * (
                                        c1 * (vz[ix][iz] - vz[ix][iz - 1]) +
                                        c2 * (vz[ix][iz - 2] - vz[ix][iz + 1])) / dz));
                txz[ix][iz] = taper[ix][iz] *
                              (txz[ix][iz] - dt * mu[ix][iz] * (
                                      (c1 * (vx[ix][iz + 1] - vx[ix][iz]) +
                                       c2 * (vx[ix][iz - 1] - vx[ix][iz + 2])) / dz +
                                      (c1 * (vz[ix][iz] - vz[ix - 1][iz]) +
                                       c2 * (vz[ix - 2][iz] - vz[ix + 1][iz])) / dx));

            }
        }
        // Reverse time integrate dynamic fields for velocity
        #pragma omp parallel for collapse(2)
        for (int ix = 2; ix < nx - 2; ++ix) {
            for (int iz = 2; iz < nz - 2; ++iz) {
                vx[ix][iz] =
                        taper[ix][iz] *
                        (vx[ix][iz]
                         - b_vx[ix][iz] * dt * (
                                (c1 * (txx[ix][iz] - txx[ix - 1][iz]) +
                                 c2 * (txx[ix - 2][iz] - txx[ix + 1][iz])) / dx +
                                (c1 * (txz[ix][iz] - txz[ix][iz - 1]) +
                                 c2 * (txz[ix][iz - 2] - txz[ix][iz + 1])) / dz));
                vz[ix][iz] =
                        taper[ix][iz] *
                        (vz[ix][iz]
                         - b_vz[ix][iz] * dt * (
                                (c1 * (txz[ix + 1][iz] - txz[ix][iz]) +
                                 c2 * (txz[ix - 1][iz] - txz[ix + 2][iz])) / dx +
                                (c1 * (tzz[ix][iz + 1] - tzz[ix][iz]) +
                                 c2 * (tzz[ix][iz - 1] - tzz[ix][iz + 2])) / dz));

            }
        }

        // Inject adjoint sources
        for (int ir = 0; ir < nr; ++ir) {
            vx[ix_receivers[ir]][iz_receivers[ir]] += dt * b_vx[ix_receivers[ir]][iz_receivers[ir]] * a_stf_ux[i_shot][ir][it] / (dx * dz);
            vz[ix_receivers[ir]][iz_receivers[ir]] += dt * b_vz[ix_receivers[ir]][iz_receivers[ir]] * a_stf_uz[i_shot][ir][it] / (dx * dz);
        }
    }

    // Output timing
    if (verbose) {
        stopTime = omp_get_wtime();
        secsElapsed = stopTime - startTime;
        std::cout << "Seconds elapsed for adjoint wave simulation: " << secsElapsed << std::endl;
    }

}

void fdWaveModel::write_receivers() { // todo rewrite to require filename manual specification
    std::string filename_ux;
    std::string filename_uz;

    std::ofstream receiver_file_ux;
    std::ofstream receiver_file_uz;

    for (int i_shot = 0; i_shot < n_shots; ++i_shot) {

        filename_ux = observed_data_folder + "/rtf_ux" + std::to_string(i_shot) + ".txt";
        filename_uz = observed_data_folder + "/rtf_uz" + std::to_string(i_shot) + ".txt";

        receiver_file_ux.open(filename_ux);
        receiver_file_uz.open(filename_uz);

        receiver_file_ux.precision(std::numeric_limits<real_simulation>::digits10 + 10);
        receiver_file_uz.precision(std::numeric_limits<real_simulation>::digits10 + 10);

        for (int i_receiver = 0; i_receiver < nr; ++i_receiver) {
            receiver_file_ux << std::endl;
            receiver_file_uz << std::endl;
            for (int it = 0; it < nt; ++it) {
                receiver_file_ux << rtf_ux[i_shot][i_receiver][it] << " ";
                receiver_file_uz << rtf_uz[i_shot][i_receiver][it] << " ";

            }
        }
        receiver_file_ux.close();
        receiver_file_uz.close();
    }
}

void fdWaveModel::write_sources() {
    std::string filename_sources;
    std::ofstream shot_file;

    for (int i_shot = 0; i_shot < n_shots; ++i_shot) {

        filename_sources = stf_folder + "/sources_shot_" + std::to_string(i_shot) + ".txt";

        shot_file.open(filename_sources);

        shot_file.precision(std::numeric_limits<real_simulation>::digits10 + 10);

        for (int i_source : which_source_to_fire_in_which_shot[i_shot]) {
            shot_file << std::endl;
            for (int it = 0; it < nt; ++it) {
                shot_file << stf[i_source][it] << " ";
            }
        }
        shot_file.close();
    }
}

void fdWaveModel::update_from_velocity() {
    #pragma omp parallel for collapse(2)
    for (int ix = 0; ix < nx; ++ix) {
        for (int iz = 0; iz < nz; ++iz) {
            mu[ix][iz] = real_simulation(pow(vs[ix][iz], 2) * rho[ix][iz]);
            lm[ix][iz] = real_simulation(pow(vp[ix][iz], 2) * rho[ix][iz]);
            la[ix][iz] = lm[ix][iz] - 2 * mu[ix][iz];
            b_vx[ix][iz] = real_simulation(1.0 / rho[ix][iz]);
            b_vz[ix][iz] = b_vx[ix][iz];
        }
    }
}

void fdWaveModel::load_receivers(bool verbose) {
    std::string filename_ux;
    std::string filename_uz;

    std::ifstream receiver_file_ux;
    std::ifstream receiver_file_uz;

    for (int i_shot = 0; i_shot < n_shots; ++i_shot) {
        filename_ux = observed_data_folder + "/rtf_ux" + std::to_string(i_shot) + ".txt";
        filename_uz = observed_data_folder + "/rtf_uz" + std::to_string(i_shot) + ".txt";

        receiver_file_ux.open(filename_ux);
        receiver_file_uz.open(filename_uz);

        // Check if the file actually exists
        if (verbose) {
            std::cout << "File for ux data at shot " << i_shot << " is "
                      << (receiver_file_ux.good() ? "good (exists at least)." : "ungood.") << std::endl;
            std::cout << "File for uz data at shot " << i_shot << " is "
                      << (receiver_file_uz.good() ? "good (exists at least)." : "ungood.") << std::endl;
        }
        if (!receiver_file_ux.good() or !receiver_file_uz.good()) {
            throw std::invalid_argument("Not all data is present!");
        }

        real_simulation placeholder_ux;
        real_simulation placeholder_uz;

        for (int i_receiver = 0; i_receiver < nr; ++i_receiver) {
            for (int it = 0; it < nt; ++it) {

                receiver_file_ux >> placeholder_ux;
                receiver_file_uz >> placeholder_uz;

                rtf_ux_true[i_shot][i_receiver][it] = placeholder_ux;
                rtf_uz_true[i_shot][i_receiver][it] = placeholder_uz;
            }
        }

        // Check data was large enough for set up
        if (!receiver_file_ux.good() or !receiver_file_uz.good()) {
            std::cout << "Received bad state of file at end of reading! Does the data match the set up?" << std::endl;
            throw std::invalid_argument("Not enough data is present!");
        }
        // Try to load more data ...
        receiver_file_ux >> placeholder_ux;
        receiver_file_uz >> placeholder_uz;
        // ... which shouldn't be possible
        if (receiver_file_ux.good() or receiver_file_uz.good()) {
            std::cout << "Received good state of file past reading! Does the data match the set up?" << std::endl;
            throw std::invalid_argument("Too much data is present!");
        }

        receiver_file_uz.close();
        receiver_file_ux.close();
    }

}

void fdWaveModel::calculate_misfit() {
    misfit = 0;
    for (int i_shot = 0; i_shot < n_shots; ++i_shot) {
        for (int i_receiver = 0; i_receiver < nr; ++i_receiver) {
            for (int it = 0; it < nt; ++it) {
                misfit += 0.5 * dt * pow(rtf_ux_true[i_shot][i_receiver][it] - rtf_ux[i_shot][i_receiver][it], 2);// /
                //data_variance_ux[i_shot][i_receiver][it];
                misfit += 0.5 * dt * pow(rtf_uz_true[i_shot][i_receiver][it] - rtf_uz[i_shot][i_receiver][it], 2);// /
                //data_variance_uz[i_shot][i_receiver][it];
            }
        }
    }
}

void fdWaveModel::calculate_adjoint_sources() {
    #pragma omp parallel for collapse(3)
    for (int is = 0; is < n_shots; ++is) {
        for (int ir = 0; ir < nr; ++ir) {
            for (int it = 0; it < nt; ++it) {
                a_stf_ux[is][ir][it] = rtf_ux[is][ir][it] - rtf_ux_true[is][ir][it];
                a_stf_uz[is][ir][it] = rtf_uz[is][ir][it] - rtf_uz_true[is][ir][it];
            }
        }
    }
}

void fdWaveModel::map_kernels_to_velocity() {
    #pragma omp parallel for collapse(2)
    for (int ix = 0; ix < nx; ++ix) {
        for (int iz = 0; iz < nz; ++iz) {
            vp_kernel[ix][iz] = 2 * vp[ix][iz] * lambda_kernel[ix][iz] / b_vx[ix][iz];
            vs_kernel[ix][iz] = (2 * vs[ix][iz] * mu_kernel[ix][iz] - 4 * vs[ix][iz] * lambda_kernel[ix][iz]) / b_vx[ix][iz];
            density_v_kernel[ix][iz] = density_l_kernel[ix][iz]
                                       + (vp[ix][iz] * vp[ix][iz] - 2 * vs[ix][iz] * vs[ix][iz]) * lambda_kernel[ix][iz]
                                       + vs[ix][iz] * vs[ix][iz] * mu_kernel[ix][iz];
        }
    }
}

void fdWaveModel::load_target(const std::string &de_target_relative_path, const std::string &vp_target_relative_path,
                              const std::string &vs_target_relative_path, bool verbose) {
    std::ifstream de_target_file;
    std::ifstream vp_target_file;
    std::ifstream vs_target_file;

    de_target_file.open(de_target_relative_path);
    vp_target_file.open(vp_target_relative_path);
    vs_target_file.open(vs_target_relative_path);

    // Check if the file actually exists
    if (verbose) {
        std::cout << "File: " << de_target_relative_path << std::endl;
        std::cout << "File for de_target is " << (de_target_file.good() ? "good (exists at least)." : "ungood.") << std::endl;
        std::cout << "File: " << vp_target_relative_path << std::endl;
        std::cout << "File for vp_target is " << (vp_target_file.good() ? "good (exists at least)." : "ungood.") << std::endl;
        std::cout << "File: " << vs_target_relative_path << std::endl;
        std::cout << "File for vs_target is " << (vs_target_file.good() ? "good (exists at least)." : "ungood.") << std::endl;
    }
    if (!de_target_file.good() or !vp_target_file.good() or !vs_target_file.good()) {
        throw std::invalid_argument("Not all data for target models is present!");
    }

    real_simulation placeholder_de;
    real_simulation placeholder_vp;
    real_simulation placeholder_vs;
    for (int ix = 0; ix < nx; ++ix) {
        for (int iz = 0; iz < nz; ++iz) {

            de_target_file >> placeholder_de;
            vp_target_file >> placeholder_vp;
            vs_target_file >> placeholder_vs;

            rho[ix][iz] = placeholder_de;
            vp[ix][iz] = placeholder_vp;
            vs[ix][iz] = placeholder_vs;
        }
    }

    // Check data was large enough for set up
    if (!de_target_file.good() or !vp_target_file.good() or !vs_target_file.good()) {
        std::cout << "Received bad state of file at end of reading. Does the data match the domain?" << std::endl;
        throw std::invalid_argument("Not enough data is present!");
    }
    // Try to load more data ...
    de_target_file >> placeholder_de;
    vp_target_file >> placeholder_vp;
    vs_target_file >> placeholder_vs;
    // ... which shouldn't be possible
    if (de_target_file.good() or vp_target_file.good() or vs_target_file.good()) {
        std::cout << "Received good state of file past reading. Does the data match the domain?" << std::endl;
        throw std::invalid_argument("Too much data is present!");
    }

    de_target_file.close();
    vp_target_file.close();
    vs_target_file.close();

    update_from_velocity();
}

void fdWaveModel::reset_velocity_fields() {
    reset_velocity_fields(true, true, true);
}

void fdWaveModel::reset_velocity_fields(bool reset_de, bool reset_vp, bool reset_vs) {
    if (reset_de) {
        for (int ix = 0; ix < nx; ++ix) {
            for (int iz = 0; iz < nz; ++iz) {
                rho[ix][iz] = starting_rho[ix][iz];
            }
        }
    }
    if (reset_vp) {
        for (int ix = 0; ix < nx; ++ix) {
            for (int iz = 0; iz < nz; ++iz) {
                vp[ix][iz] = starting_vp[ix][iz];
            }
        }
    }
    if (reset_vs) {
        for (int ix = 0; ix < nx; ++ix) {
            for (int iz = 0; iz < nz; ++iz) {
                vs[ix][iz] = starting_vs[ix][iz];
            }
        }
    }
    update_from_velocity();
}

void fdWaveModel::load_starting(const std::string &de_starting_relative_path, const std::string &vp_starting_relative_path,
                                const std::string &vs_starting_relative_path, bool verbose) {
    std::ifstream de_starting_file;
    std::ifstream vp_starting_file;
    std::ifstream vs_starting_file;

    de_starting_file.open(de_starting_relative_path);
    vp_starting_file.open(vp_starting_relative_path);
    vs_starting_file.open(vs_starting_relative_path);

    // Check if the file actually exists
    if (verbose) {
        std::cout << "File for de_starting is " << (de_starting_file.good() ? "good (exists at least)." : "ungood.") << std::endl;
        std::cout << "File for vp_starting is " << (vp_starting_file.good() ? "good (exists at least)." : "ungood.") << std::endl;
        std::cout << "File for vs_starting is " << (vs_starting_file.good() ? "good (exists at least)." : "ungood.") << std::endl;
    }
    if (!de_starting_file.good() or !vp_starting_file.good() or !vs_starting_file.good()) {
        throw std::invalid_argument("Not all data is present!");
    }

    real_simulation placeholder_de;
    real_simulation placeholder_vp;
    real_simulation placeholder_vs;
    for (int ix = 0; ix < nx; ++ix) {
        for (int iz = 0; iz < nz; ++iz) {

            de_starting_file >> placeholder_de;
            vp_starting_file >> placeholder_vp;
            vs_starting_file >> placeholder_vs;

            starting_rho[ix][iz] = placeholder_de;
            starting_vp[ix][iz] = placeholder_vp;
            starting_vs[ix][iz] = placeholder_vs;
        }
    }

    // Check data was large enough for set up
    if (!de_starting_file.good() or !vp_starting_file.good() or !vs_starting_file.good()) {
        std::cout << "Received bad state of file at end of reading. Does the data match the domain?" << std::endl;
        throw std::invalid_argument("Not enough data is present!");
    }
    // Try to load more data ...
    de_starting_file >> placeholder_de;
    vp_starting_file >> placeholder_vp;
    vs_starting_file >> placeholder_vs;
    // ... which shouldn't be possible
    if (de_starting_file.good() or vp_starting_file.good() or vs_starting_file.good()) {
        std::cout << "Received good state of file past reading. Does the data match the domain?" << std::endl;
        throw std::invalid_argument("Too much data is present!");
    }

    de_starting_file.close();
    vp_starting_file.close();
    vs_starting_file.close();

    reset_velocity_fields();

    update_from_velocity();
}

void fdWaveModel::run_model(bool verbose) {
    for (int i_shot = 0; i_shot < n_shots; ++i_shot) {
        forward_simulate(i_shot, true, verbose);
    }
    calculate_misfit();
    calculate_adjoint_sources();
    reset_kernels();
    for (int is = 0; is < n_shots; ++is) {
        adjoint_simulate(is, verbose);
    }
    map_kernels_to_velocity();
}

void fdWaveModel::run_model(bool verbose, bool simulate_adjoint) {
    for (int i_shot = 0; i_shot < n_shots; ++i_shot) {
        forward_simulate(i_shot, true, verbose);
    }
    calculate_misfit();
    if (simulate_adjoint) {
        calculate_adjoint_sources();
        reset_kernels();
        for (int is = 0; is < n_shots; ++is) {
            adjoint_simulate(is, verbose);
        }
        map_kernels_to_velocity();
    }
}

void fdWaveModel::reset_kernels() {
    std::fill(*lambda_kernel, &lambda_kernel[nx - 1][nz - 1] + 1, 0.0);
    std::fill(*mu_kernel, &mu_kernel[nx - 1][nz - 1] + 1, 0.0);
    std::fill(*density_l_kernel, &density_l_kernel[nx - 1][nz - 1] + 1, 0.0);
}

void fdWaveModel::set_2d_array_to_zero(real_simulation **pDouble) {
//    std::fill(&pDouble[0][0], &pDouble[0][0] + sizeof(pDouble) / sizeof(int), 0);
}

// Allocation and deallocation

void fdWaveModel::allocate_1d_array(real_simulation *&pDouble, int dim1) {
    pDouble = new real_simulation[dim1];
}

void fdWaveModel::allocate_2d_array(real_simulation **&pDouble, const int dim1, const int dim2) {
    pDouble = new real_simulation *[dim1];
    for (int i = 0; i < dim1; ++i)
        allocate_1d_array(pDouble[i], dim2);
}

void fdWaveModel::allocate_3d_array(real_simulation ***&pDouble, int dim1, int dim2, int dim3) {
    pDouble = new real_simulation **[dim1];
    for (int i = 0; i < dim1; ++i)
        allocate_2d_array(pDouble[i], dim2, dim3);
}

void fdWaveModel::allocate_4d_array(real_simulation ****&pDouble, int dim1, int dim2, int dim3, int dim4) {
    pDouble = new real_simulation ***[dim1];
    for (int i = 0; i < dim1; ++i)
        allocate_3d_array(pDouble[i], dim2, dim3, dim4);
}

void fdWaveModel::deallocate_1d_array(real_simulation *&pDouble) {
    delete[] pDouble;
    pDouble = nullptr;
}

void fdWaveModel::deallocate_2d_array(real_simulation **&pDouble, const int dim1) {
    for (int i = 0; i < dim1; i++) {
        deallocate_1d_array(pDouble[i]);
    }
    delete[] pDouble;
    pDouble = nullptr;
}

void fdWaveModel::deallocate_3d_array(real_simulation ***&pDouble, const int dim1, const int dim2) {
    for (int i = 0; i < dim1; i++) {
        deallocate_2d_array(pDouble[i], dim2);
    }
    delete[] pDouble;
    pDouble = nullptr;
}

void fdWaveModel::deallocate_4d_array(real_simulation ****&pDouble, const int dim1, const int dim2, const int dim3) {
    for (int i = 0; i < dim1; i++) {
        deallocate_3d_array(pDouble[i], dim2, dim3);
    }
    delete[] pDouble;
    pDouble = nullptr;
}