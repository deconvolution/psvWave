//
// Created by lars on 25.01.19.
//

#ifndef FDMODEL_H
#define FDMODEL_H

#include <string>

#include <vector>

#include "Eigen/Dense"
#include "Eigen/Sparse"

#include "contiguous_arrays.h"
#include "Metal/Metal.hpp"
#include "Foundation/Foundation.hpp"
#include "QuartzCore/QuartzCore.hpp"

#include "MetalOperations.hpp"

//! Typedef that is a shorthand for the correct precision column vector.
//! This vector has the right precision and shape to be used in matrix equations.
//! It is of dynamic size.
using dynamic_vector = Eigen::Matrix<float, Eigen::Dynamic, 1>;

//! \brief Finite difference wave modelling class.
//!
//! This class contains everything needed to do finite difference wave forward
//! and adjoint modelling.  It contains the entire experimental parameters as
//! fields, which are loaded at runtime from the supplied .ini file. The class
//! contains all necessary functions to perform FWI, but lacks optimization
//! schemes.
class fdModel
{
public:
  // ---- CONSTRUCTORS AND DESTRUCTORS ----
  //!  \brief Constructor for modelling class.
  //!
  //!  This constructor creates the modelling class from a configuration file
  //!  supplied at runtime. As such, all fields are created dynamically.
  //!
  //!  @param configuration_file_relative_path Relative path to the configuration
  //!  .ini file. This file should contain all the fields needed for simulation.
  //!  Arbitrary defaults are hardcoded into the binary as backup within the
  //!  parse_configuration() method.
  explicit fdModel(MTL::Device *gpu_device, const char *configuration_file_relative_path);

  fdModel(MTL::Device *gpu_device, const int nt, const int nx_inner, const int nz_inner,
          const int nx_inner_boundary, const int nz_inner_boundary,
          const float dx, const float dz, const float dt,
          const int np_boundary, const float np_factor,
          const float scalar_rho, const float scalar_vp,
          const float scalar_vs, const int npx, const int npz,
          const float peak_frequency, const float source_timeshift,
          const float delay_cycles_per_shot, const int n_sources,
          const int n_shots, const std::vector<int> ix_sources_vector,
          const std::vector<int> iz_sources_vector,
          const std::vector<float> moment_angles_vector,
          const std::vector<std::vector<int>> which_source_to_fire_in_which_shot,
          const int nr, const std::vector<int> ix_receivers_vector,
          const std::vector<int> iz_receivers_vector, const int snapshot_interval,
          const std::string observed_data_folder, const std::string stf_folder);

  fdModel(MTL::Device *gpu_device, const fdModel &model);

  //!  \brief Destructor for the class.
  //!
  //!  The destructor properly addresses every used new keyword in the
  //!  constructor, freeing all memory.
  ~fdModel();

  void allocate_memory();

  void initialize_arrays();
  void copy_arrays(const fdModel &model);

  void parse_parameters(const std::vector<int> ix_sources_vector,
                        const std::vector<int> iz_sources_vector,
                        const std::vector<float> moment_angles_vector,
                        const std::vector<int> ix_receivers_vector,
                        const std::vector<int> iz_receivers_vector);

  // ---- METHODS ----
  //!  \brief Method that parses .ini configuration file. Only used in
  //!  fdModel().
  //!
  //!  @param configuration_file_relative_path Relative path to the configuration
  //!  .ini file.
  void parse_configuration_file(const char *configuration_file_relative_path);

  //!  \brief Method to forward simulate wavefields for a specific shot.
  //!
  //!  Forward simulate wavefields of shot i_shot based on currently loaded
  //!  models. Storage of wavefields and verbosity of run can be toggled.
  //!
  //!  @param i_shot Integer controlling which shot to simulate.
  //!  @param store_fields Boolean to control storage of wavefields. If storage is
  //!  not required (i.e. no adjoint modeling), forward simulation should be
  //!  faster without storage.
  //!  @param verbose Boolean controlling if modelling should be verbose.
  void forward_simulate(int i_shot, bool store_fields, bool verbose,
                        bool output_wavefields = false);

  //!  \brief Method to adjoint simulate wavefields for a specific shot.
  //!
  //!  Adjoint simulate wavefields of shot i_shot based on currently loaded models
  //!  and calculate adjoint sources. Verbosity of run can be toggled.
  //!
  //!  @param i_shot Integer controlling which shot to simulate.
  //!  @param verbose Boolean controlling if modelling should be verbose.
  void adjoint_simulate(int i_shot, bool verbose);

  //!  \brief Method to write out synthetic seismograms to plaintext.
  //!
  //!  This method writes out the synthetic seismograms to a plaintext file.
  //!  Allows one to e.g. subsequently import these files as observed seismograms
  //!  later using load_receivers(). Every shot generates a separate ux and uz
  //!  receiver file (rtf_ux/rtf_uz), with every receiver being a single line in
  //!  these files.
  void write_receivers();

  void write_receivers(std::string prefix);

  //!  \brief Method to write out source signals to plaintext.
  //!
  //!  This method writes out the source time function (without moment tensor) to
  //!  plaintext file. Useful for e.g. visualizing the source staggering.
  void write_sources();

  //!  \brief Method to load receiver files.
  //!
  //!  This method loads receiver data from observed_data_folder folder into the
  //!  object. The data has to exactly match the set-up, and be named according to
  //!  component and shot (as generated by write_receivers() ).
  //!
  //!  @param verbose Controls the verbosity of the method during loading.
  void load_receivers(bool verbose);

  //!  \brief Method to map kernels to velocity parameter set.
  //!
  //!  This method takes the kernels (lambda, mu, rho) as originally calculated on
  //!  the Lamé's parameter set and maps them to the velocity parameter set (vp,
  //!  vs, rho).
  void map_kernels_to_velocity();

  //!  \brief Method to map velocities into Lamé's parameter set.
  //!
  //!  This method updates Lamé's parameters of the current model based on the
  //!  velocity parameters of the current model. Typically has to be done every
  //!  time after updating velocity.
  void update_from_velocity();

  //!  \brief Method to calculate L2 misfit.
  //!
  //!  This method calculates L2 misfit between observed seismograms and synthetic
  //!  seismograms and stores it in the misfit field.
  void calculate_l2_misfit();

  //!  \brief Method to calculate L2 adjoint sources
  //!  This method calculates L2 misfit between observed seismograms and synthetic
  //!  seismograms and stores it in the misfit field.
  void calculate_l2_adjoint_sources();

  //!  \brief Method to load models from plaintext into the model.
  //!
  //!  This methods loads any appropriate model (expressed in density, P-wave
  //!  velocity, and S-wave velocity) into the class and updates the Lamé fields
  //!  accordingly.
  //!
  //!  @param de_path Relative path to plaintext de file.
  //!  @param vp_path Relative path to plaintext vp file.
  //!  @param vs_path Relative path to plaintext vs file.
  //!  @param verbose Boolean controlling the verbosity of the method.
  void load_model(const std::string &de_path, const std::string &vp_path,
                  const std::string &vs_path, bool verbose);

  //!  \brief Method to perform all steps necessary for FWI, with additional
  //!  control over adjoint simulation.
  //!
  //!  This method performs all necessary steps in FWI; forward modelling, misfit
  //!  calculation and optionally adjoint source calculation, adjoint modelling
  //!  and kernel projection.
  //!
  //!  @param verbose Boolean controlling the verbosity of the method.
  //!  @param simulate_adjoint Boolean controlling the execution of the adjoint
  //!  simulation and kernel computation.
  void run_model(bool verbose, bool simulate_adjoint);

  //!  \brief Method to reset all Lamé sensitivity kernels to zero.
  //!
  //!  This method resets all sensitivity kernels to zero. Essential before
  //!  performing new adjoint simulations, as otherwise the kernels of subsequent
  //!  simulations would stack.
  void reset_kernels();

  MTL::Device *gpu_device;
  MetalOperations *mtl_ops;

  // ----  FIELDS ----
  // |--< Utility fields >--
  // | Finite difference coefficients
  float c1 = float(9.0 / 8.0);
  float c2 = float(1.0 / 24.0);
  // Todo refactor into configuration
  bool add_np_to_source_location = true;
  bool add_np_to_receiver_location = true;

  // |--< Spatial fields >--
  // | Dynamic physical fields
  float *vx;  //!< Dynamic horizontal velocity field used in the simulations.
  float *vz;  //!< Dynamic vertical velocity field used in the simulations.
  float *txx; //!< Dynamic horizontal stress field used in the simulations.
  float *tzz; //!< Dynamic vertical stress field used in the simulations.
  float *txz; //!< Dynamic shear stress field used in the simulations.

  MTL::Buffer *vx_gpu;
  MTL::Buffer *vz_gpu;
  MTL::Buffer *txx_gpu;
  MTL::Buffer *tzz_gpu;
  MTL::Buffer *txz_gpu;

  // | Static physical fields
  float *lm;
  float *la;
  float *mu;
  float *b_vx;
  float *b_vz;
  float *rho;
  float *vp;
  float *vs;

  MTL::Buffer *lm_gpu;
  MTL::Buffer *la_gpu;
  MTL::Buffer *mu_gpu;
  MTL::Buffer *b_vx_gpu;
  MTL::Buffer *b_vz_gpu;
  MTL::Buffer *rho_gpu;
  MTL::Buffer *vp_gpu;
  MTL::Buffer *vs_gpu;

  // | Sensitivity kernels in Lamé's basis
  float *lambda_kernel;
  float *mu_kernel;
  float *density_l_kernel;

  MTL::Buffer *lambda_kernel_gpu;
  MTL::Buffer *mu_kernel_gpu;
  MTL::Buffer *density_l_kernel_gpu;

  // | Sensitivity kernels in velocity basis
  float *vp_kernel;
  float *vs_kernel;
  float *density_v_kernel;

  MTL::Buffer *vp_kernel_gpu;
  MTL::Buffer *vs_kernel_gpu;
  MTL::Buffer *density_v_kernel_gpu;

  // | Static physical fields for the starting model
  float *starting_rho;
  float *starting_vp;
  float *starting_vs;
  float *taper;

  MTL::Buffer *starting_rho_gpu;
  MTL::Buffer *starting_vp_gpu;
  MTL::Buffer *starting_vs_gpu;
  MTL::Buffer *taper_gpu;

  // |--< Time dependent signals >--
  float *t;
  float *stf;
  float *moment;
  float *rtf_ux;
  float *rtf_uz;
  float *rtf_ux_true;
  float *rtf_uz_true;
  float *a_stf_ux;
  float *a_stf_uz;
  float *accu_vx;
  float *accu_vz;
  float *accu_txx;
  float *accu_tzz;
  float *accu_txz;

  MTL::Buffer *t_gpu;
  MTL::Buffer *stf_gpu;
  MTL::Buffer *moment_gpu;
  MTL::Buffer *rtf_ux_gpu;
  MTL::Buffer *rtf_uz_gpu;
  MTL::Buffer *rtf_ux_true_gpu;
  MTL::Buffer *rtf_uz_true_gpu;
  MTL::Buffer *a_stf_ux_gpu;
  MTL::Buffer *a_stf_uz_gpu;
  MTL::Buffer *accu_vx_gpu;
  MTL::Buffer *accu_vz_gpu;
  MTL::Buffer *accu_txx_gpu;
  MTL::Buffer *accu_tzz_gpu;
  MTL::Buffer *accu_txz_gpu;

  std::vector<int> shape_grid;
  std::vector<int> shape_t;
  std::vector<int> shape_0; // scalars
  std::vector<int> shape_stf;
  std::vector<int> shape_moment;
  std::vector<int> shape_receivers;
  std::vector<int> shape_accu;

  // -- Definition of simulation --
  // | Domain
  int nt;
  int nx_inner;
  int nz_inner;
  int nx_inner_boundary;
  int nz_inner_boundary;
  float *dx;
  float *dz;
  float *dt;

  MTL::Buffer *dx_gpu;
  MTL::Buffer *dz_gpu;
  MTL::Buffer *dt_gpu;

  // | Boundary
  int np_boundary;
  float np_factor;
  // | Medium
  float scalar_rho;
  float scalar_vp;
  float scalar_vs;
  // | Sources
  int n_sources;
  int n_shots;
  std::vector<std::vector<int>> which_source_to_fire_in_which_shot;
  float delay_cycles_per_shot; // over f
  int *ix_sources;
  int *iz_sources;
  float *moment_angles;
  float peak_frequency;
  float alpha;
  float t0;
  int nr;
  int *ix_receivers;
  int *iz_receivers;
  int snapshot_interval;

  int snapshots;
  int nx;
  int nz;
  int nx_free_parameters;
  int nz_free_parameters;

  int basis_gridpoints_x = 1; // How many gridpoints there are in a basis function
  int basis_gridpoints_z = 1;
  int free_parameters;

  // -- Helper stuff for inverse problems --
  // float data_variance_ux[n_shots][nr][nt];
  // float data_variance_uz[n_shots][nr][nt];

  float misfit;
  std::string observed_data_folder;
  std::string stf_folder;

  void write_kernels();

  dynamic_vector get_model_vector();
  void set_model_vector(dynamic_vector m);
  dynamic_vector get_gradient_vector();
  dynamic_vector load_vector(const std::string &vector_path, bool verbose);
};

// Miscellaneous functions

//! \brief Function to parse strings containing lists to std::vectors.
//!
//! Parses any string in the format {a, b, c, d, ...} to a vector of int, float,
//! double. Only types that can be cast to floats are supported for now. This is
//! due to the usage of strtof(); no template variant is used as of yet. The
//! input string is allowed to be trailed by a comment leading with a semicolon.
//! Items are appended to the passed vector.
//!
//! @param T Arbitrary numeric type that has a cast to and from float.
//! @param string_to_parse Input string of the form "{<item1>, <item2>, <item3>,
//! ... } ; comment ".
//! @param destination_vector Pointer to an (not necessarily empty) vector of
//! suitable type.
template <class T>
void parse_string_to_vector(std::basic_string<char> string_to_parse,
                            std::vector<T> *destination_vector);

//! \brief Function to parse strings containing 2d integer lists to
//! std::vector<std::vector<int>>. Sublists do not need to be of the same length.
//!
//! @param string_to_parse Input string of the form "{{<item1.1>, <item1.2>,
//! < item1.3>, ...}, {<item2.1>, <item2.2>,
//! ...}, ... } ; comment ".
//! @param destination_vector Pointer to an (not necessarily empty)
//! std::vector<std::vector<int>>.
void parse_string_to_nested_int_vector(
    std::basic_string<char> string_to_parse,
    std::vector<std::vector<int>> *destination_vector);

std::string zero_pad_number(int num, int pad);

#endif // FDMODEL_H
