#include <fstream>
#include <iostream>

#include "mfem.hpp"

#include "axom/slic/core/SimpleLogger.hpp"

#include "serac/serac_config.hpp"
#include "serac/numerics/mesh_utils.hpp"
#include "serac/numerics/expr_template_ops.hpp"
#include "serac/physics/operators/stdfunction_operator.hpp"
#include "serac/physics/utilities/variational_form/weak_form.hpp"
#include "serac/physics/utilities/variational_form/tensor.hpp"
#include "serac/physics/utilities/variational_form/integral.hpp"

#include <gtest/gtest.h>

using namespace std;
using namespace mfem;
using namespace serac;

double tol = 1.e-13;
int num_procs, myid;

constexpr bool                 verbose = false;
std::unique_ptr<mfem::ParMesh> mesh2D;
std::unique_ptr<mfem::ParMesh> mesh3D;

template <int p, int dim>
void weak_form_test(mfem::ParMesh& mesh, H1<p> test, H1<p> trial, Dimension<dim>)
{
  static constexpr double a = 1.7;
  static constexpr double b = 2.1;

  auto                  fec = H1_FECollection(p, dim);
  ParFiniteElementSpace fespace(&mesh, &fec);

  ParBilinearForm A(&fespace);

  ConstantCoefficient a_coef(a);
  A.AddDomainIntegrator(new MassIntegrator(a_coef));

  ConstantCoefficient b_coef(b);
  A.AddDomainIntegrator(new DiffusionIntegrator(b_coef));
  A.Assemble(0);
  A.Finalize();
  std::unique_ptr<mfem::HypreParMatrix> J(A.ParallelAssemble());

  ParLinearForm       f(&fespace);
  FunctionCoefficient load_func([&](const Vector& coords) { return 100 * coords(0) * coords(1); });

  f.AddDomainIntegrator(new DomainLFIntegrator(load_func));
  f.Assemble();
  std::unique_ptr<mfem::HypreParVector> F(f.ParallelAssemble());

  ParGridFunction u_global(&fespace);
  u_global.Randomize();

  Vector U(fespace.TrueVSize());
  u_global.GetTrueDofs(U);

  using test_space  = decltype(test);
  using trial_space = decltype(trial);

  WeakForm<test_space(trial_space)> residual(&fespace, &fespace);

  residual.AddDomainIntegral(
      Dimension<dim>{},
      [&](auto x, auto temperature) {
        auto [u, du_dx] = temperature;
        auto f0         = a * u - (100 * x[0] * x[1]);
        auto f1         = b * du_dx;
        return std::tuple{f0, f1};
      },
      mesh);

  mfem::Vector r1 = (*J) * U - (*F);
  mfem::Vector r2 = residual(U);

  if (verbose) {
    std::cout << "||r1||: " << r1.Norml2() << std::endl;
    std::cout << "||r2||: " << r2.Norml2() << std::endl;
    std::cout << "||r1-r2||/||r1||: " << mfem::Vector(r1 - r2).Norml2() / r1.Norml2() << std::endl;
  }
  EXPECT_NEAR(0., mfem::Vector(r1 - r2).Norml2() / r1.Norml2(), tol);

  mfem::Operator& grad2 = residual.GetGradient(U);

  mfem::Vector g1 = (*J) * U;
  mfem::Vector g2 = grad2 * U;

  if (verbose) {
    std::cout << "||g1||: " << g1.Norml2() << std::endl;
    std::cout << "||g2||: " << g2.Norml2() << std::endl;
    std::cout << "||g1-g2||/||g1||: " << mfem::Vector(g1 - g2).Norml2() / g1.Norml2() << std::endl;
  }
  EXPECT_NEAR(0., mfem::Vector(g1 - g2).Norml2() / g1.Norml2(), tol);
}

template <int p, int dim>
void weak_form_test(mfem::ParMesh& mesh, H1<p, dim> test, H1<p, dim> trial, Dimension<dim>)
{
  static constexpr double a = 1.7;
  static constexpr double b = 2.1;

  auto                  fec = H1_FECollection(p, dim);
  ParFiniteElementSpace fespace(&mesh, &fec, dim);

  ParBilinearForm A(&fespace);

  ConstantCoefficient a_coef(a);
  A.AddDomainIntegrator(new VectorMassIntegrator(a_coef));

  ConstantCoefficient lambda_coef(b);
  ConstantCoefficient mu_coef(b);
  A.AddDomainIntegrator(new ElasticityIntegrator(lambda_coef, mu_coef));
  A.Assemble(0);
  A.Finalize();

  std::unique_ptr<mfem::HypreParMatrix> J(A.ParallelAssemble());

  ParLinearForm             f(&fespace);
  VectorFunctionCoefficient load_func(dim, [&](const Vector& /*coords*/, Vector& force) {
    force    = 0.0;
    force(0) = -1.0;
  });

  f.AddDomainIntegrator(new VectorDomainLFIntegrator(load_func));
  f.Assemble();
  std::unique_ptr<mfem::HypreParVector> F(f.ParallelAssemble());

  ParGridFunction u_global(&fespace);
  u_global.Randomize();

  Vector U(fespace.TrueVSize());
  u_global.GetTrueDofs(U);

  [[maybe_unused]] static constexpr auto I = Identity<dim>();

  using test_space  = decltype(test);
  using trial_space = decltype(trial);

  WeakForm<test_space(trial_space)> residual(&fespace, &fespace);

  residual.AddDomainIntegral(
      Dimension<dim>{},
      [&](auto /*x*/, auto displacement) {
        auto [u, du_dx] = displacement;
        auto f0         = a * u + I[0];
        auto strain     = 0.5 * (du_dx + transpose(du_dx));
        auto f1         = b * tr(strain) * I + 2.0 * b * strain;
        return std::tuple{f0, f1};
      },
      mesh);

  mfem::Vector r1 = (*J) * U - (*F);
  mfem::Vector r2 = residual(U);

  if (verbose) {
    std::cout << "||r1||: " << r1.Norml2() << std::endl;
    std::cout << "||r2||: " << r2.Norml2() << std::endl;
    std::cout << "||r1-r2||/||r1||: " << mfem::Vector(r1 - r2).Norml2() / r1.Norml2() << std::endl;
  }
  EXPECT_NEAR(0., mfem::Vector(r1 - r2).Norml2() / r1.Norml2(), tol);

  mfem::Operator& grad = residual.GetGradient(U);

  mfem::Vector g1 = (*J) * U;
  mfem::Vector g2 = grad * U;

  if (verbose) {
    std::cout << "||g1||: " << g1.Norml2() << std::endl;
    std::cout << "||g2||: " << g2.Norml2() << std::endl;
    std::cout << "||g1-g2||/||g1||: " << mfem::Vector(g1 - g2).Norml2() / g1.Norml2() << std::endl;
  }
  EXPECT_NEAR(0., mfem::Vector(g1 - g2).Norml2() / g1.Norml2(), tol);
}

template <int p, int dim>
void weak_form_test(mfem::ParMesh& mesh, Hcurl<p> test, Hcurl<p> trial, Dimension<dim>)
{
  static constexpr double a = 1.7;
  static constexpr double b = 2.1;

  auto                  fec = ND_FECollection(p, dim);
  ParFiniteElementSpace fespace(&mesh, &fec);

  ParBilinearForm B(&fespace);

  ConstantCoefficient a_coef(a);
  B.AddDomainIntegrator(new VectorFEMassIntegrator(a_coef));

  ConstantCoefficient b_coef(b);
  B.AddDomainIntegrator(new CurlCurlIntegrator(b_coef));
  B.Assemble(0);
  B.Finalize();
  std::unique_ptr<mfem::HypreParMatrix> J(B.ParallelAssemble());

  ParLinearForm             f(&fespace);
  VectorFunctionCoefficient load_func(dim, [&](const Vector& coords, Vector& output) {
    double x  = coords(0);
    double y  = coords(1);
    output    = 0.0;
    output(0) = 10 * x * y;
    output(1) = -5 * (x - y) * y;
  });

  f.AddDomainIntegrator(new VectorFEDomainLFIntegrator(load_func));
  f.Assemble();
  std::unique_ptr<mfem::HypreParVector> F(f.ParallelAssemble());

  ParGridFunction u_global(&fespace);
  u_global.Randomize();

  Vector U(fespace.TrueVSize());
  u_global.GetTrueDofs(U);

  using test_space  = decltype(test);
  using trial_space = decltype(trial);

  WeakForm<test_space(trial_space)> residual(&fespace, &fespace);

  residual.AddDomainIntegral(
      Dimension<dim>{},
      [&](auto x, auto vector_potential) {
        auto [A, curl_A] = vector_potential;
        auto f0          = a * A - tensor<double, dim>{10 * x[0] * x[1], -5 * (x[0] - x[1]) * x[1]};
        auto f1          = b * curl_A;
        return std::tuple{f0, f1};
      },
      mesh);

  mfem::Vector r1 = (*J) * U - (*F);
  mfem::Vector r2 = residual(U);

  if (verbose) {
    std::cout << "||r1||: " << r1.Norml2() << std::endl;
    std::cout << "||r2||: " << r2.Norml2() << std::endl;
    std::cout << "||r1-r2||/||r1||: " << mfem::Vector(r1 - r2).Norml2() / r1.Norml2() << std::endl;
  }
  EXPECT_NEAR(0., mfem::Vector(r1 - r2).Norml2() / r1.Norml2(), tol);

  mfem::Operator& grad = residual.GetGradient(U);

  mfem::Vector g1 = (*J) * U;
  mfem::Vector g2 = grad * U;

  if (verbose) {
    std::cout << "||g1||: " << g1.Norml2() << std::endl;
    std::cout << "||g2||: " << g2.Norml2() << std::endl;
    std::cout << "||g1-g2||/||g1||: " << mfem::Vector(g1 - g2).Norml2() / g1.Norml2() << std::endl;
  }
  EXPECT_NEAR(0., mfem::Vector(g1 - g2).Norml2() / g1.Norml2(), tol);
}

template <int p, int dim>
void weak_form_matrix_test(mfem::ParMesh& mesh, H1<p> test, H1<p> trial, Dimension<dim>)
{
  static constexpr double a = 1.7;
  static constexpr double b = 2.1;

  auto                  fec = H1_FECollection(p, dim);
  ParFiniteElementSpace fespace(&mesh, &fec);

  ParBilinearForm A(&fespace);

  ConstantCoefficient a_coef(a);
  A.AddDomainIntegrator(new MassIntegrator(a_coef));

  ConstantCoefficient b_coef(b);
  A.AddDomainIntegrator(new DiffusionIntegrator(b_coef));
  A.Assemble(0);
  A.Finalize();
  std::unique_ptr<mfem::HypreParMatrix> J(A.ParallelAssemble());

  ParLinearForm       f(&fespace);
  FunctionCoefficient load_func([&](const Vector& coords) { return 100 * coords(0) * coords(1); });

  f.AddDomainIntegrator(new DomainLFIntegrator(load_func));
  f.Assemble();
  std::unique_ptr<mfem::HypreParVector> F(f.ParallelAssemble());

  ParGridFunction u_global(&fespace);
  u_global.Randomize();

  Vector U(fespace.TrueVSize());
  u_global.GetTrueDofs(U);

  using test_space  = decltype(test);
  using trial_space = decltype(trial);

  WeakForm<test_space(trial_space)> residual(&fespace, &fespace);

  residual.AddDomainIntegral(
      Dimension<dim>{},
      [&](auto x, auto temperature) {
        auto [u, du_dx] = temperature;
        auto f0         = a * u - (100 * x[0] * x[1]);
        auto f1         = b * du_dx;
        return std::tuple{f0, f1};
      },
      mesh);

  mfem::Vector r1 = (*J) * U - (*F);
  mfem::Vector r2 = residual(U);

  if (verbose) {
    std::cout << "||r1||: " << r1.Norml2() << std::endl;
    std::cout << "||r2||: " << r2.Norml2() << std::endl;
    std::cout << "||r1-r2||/||r1||: " << mfem::Vector(r1 - r2).Norml2() / r1.Norml2() << std::endl;
  }
  EXPECT_NEAR(0., mfem::Vector(r1 - r2).Norml2() / r1.Norml2(), tol);

  mfem::Operator& grad2 = residual.GetGradient(U);

  mfem::Vector g1 = (*J) * U;
  mfem::Vector g2 = grad2 * U;

  if (verbose) {
    std::cout << "||g1||: " << g1.Norml2() << std::endl;
    std::cout << "||g2||: " << g2.Norml2() << std::endl;
    std::cout << "||g1-g2||/||g1||: " << mfem::Vector(g1 - g2).Norml2() / g1.Norml2() << std::endl;
  }
  EXPECT_NEAR(0., mfem::Vector(g1 - g2).Norml2() / g1.Norml2(), tol);

  Array< int > dofs;
  fespace.GetElementDofs (0, dofs);
  mfem::Vector K_e(mesh.GetNE() * dofs.Size() * fespace.GetVDim() * dofs.Size() * fespace.GetVDim() );
  residual.GradientMatrix(K_e);
  std::cout << "K_e: (" << K_e.Size() << ")" << std::endl << std::endl;;
  K_e.Print();

  mfem::Vector K_e_mfem(mesh.GetNE() * dofs.Size() * fespace.GetVDim() * dofs.Size() * fespace.GetVDim() );
  {
    auto dk = mfem::Reshape(K_e_mfem.ReadWrite(), dofs.Size() * fespace.GetVDim(), dofs.Size() * fespace.GetVDim(), mesh.GetNE());
    for (int e = 0; e < mesh.GetNE(); e++) {
      DenseMatrix mat;
      A.ComputeElementMatrix(e, mat);
      for (int i = 0; i < mat.Height(); i++) {
	for (int j = 0; j < mat.Width(); j++) {
	  dk(i,j,e) = mat(i,j);
	}
      }
    }
  }
  std::cout << "K_e_mfem: (" << K_e.Size() << ")" << std::endl;
  K_e_mfem.Print();
}


TEST(thermal, 2D_linear) { weak_form_test(*mesh2D, H1<1>{}, H1<1>{}, Dimension<2>{}); }
TEST(thermal, 2D_quadratic) { weak_form_test(*mesh2D, H1<2>{}, H1<2>{}, Dimension<2>{}); }
TEST(thermal, 2D_cubic) { weak_form_test(*mesh2D, H1<3>{}, H1<3>{}, Dimension<2>{}); }

TEST(thermal, 2D_linear_mat) { weak_form_matrix_test(*mesh2D, H1<1>{}, H1<1>{}, Dimension<2>{}); }

// TEST(thermal, 3D_linear) { weak_form_test(*mesh3D, H1<1>{}, H1<1>{}, Dimension<3>{}); }
// TEST(thermal, 3D_quadratic) { weak_form_test(*mesh3D, H1<2>{}, H1<2>{}, Dimension<3>{}); }
// TEST(thermal, 3D_cubic) { weak_form_test(*mesh3D, H1<3>{}, H1<3>{}, Dimension<3>{}); }

// TEST(hcurl, 2D_linear) { weak_form_test(*mesh2D, Hcurl<1>{}, Hcurl<1>{}, Dimension<2>{}); }
// TEST(hcurl, 2D_quadratic) { weak_form_test(*mesh2D, Hcurl<2>{}, Hcurl<2>{}, Dimension<2>{}); }
// TEST(hcurl, 2D_cubic) { weak_form_test(*mesh2D, Hcurl<3>{}, Hcurl<3>{}, Dimension<2>{}); }

// TEST(hcurl, 3D_linear) { weak_form_test(*mesh3D, Hcurl<1>{}, Hcurl<1>{}, Dimension<3>{}); }
// TEST(hcurl, 3D_quadratic) { weak_form_test(*mesh3D, Hcurl<2>{}, Hcurl<2>{}, Dimension<3>{}); }
// TEST(hcurl, 3D_cubic) { weak_form_test(*mesh3D, Hcurl<3>{}, Hcurl<3>{}, Dimension<3>{}); }

// TEST(elasticity, 2D_linear) { weak_form_test(*mesh2D, H1<1, 2>{}, H1<1, 2>{}, Dimension<2>{}); }
// TEST(elasticity, 2D_quadratic) { weak_form_test(*mesh2D, H1<2, 2>{}, H1<2, 2>{}, Dimension<2>{}); }
// TEST(elasticity, 2D_cubic) { weak_form_test(*mesh2D, H1<3, 2>{}, H1<3, 2>{}, Dimension<2>{}); }

// TEST(elasticity, 3D_linear) { weak_form_test(*mesh3D, H1<1, 3>{}, H1<1, 3>{}, Dimension<3>{}); }
// TEST(elasticity, 3D_quadratic) { weak_form_test(*mesh3D, H1<2, 3>{}, H1<2, 3>{}, Dimension<3>{}); }
// TEST(elasticity, 3D_cubic) { weak_form_test(*mesh3D, H1<3, 3>{}, H1<3, 3>{}, Dimension<3>{}); }

int main(int argc, char* argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);

  axom::slic::SimpleLogger logger;

  int serial_refinement   = 1;
  int parallel_refinement = 0;

  std::string meshfile2D = SERAC_REPO_DIR "/data/meshes/star.mesh";
  mesh2D = mesh::refineAndDistribute(buildMeshFromFile(meshfile2D), serial_refinement, parallel_refinement);

  std::string meshfile3D = SERAC_REPO_DIR "/data/meshes/beam-hex.mesh";
  mesh3D = mesh::refineAndDistribute(buildMeshFromFile(meshfile3D), serial_refinement, parallel_refinement);

  int result = RUN_ALL_TESTS();

  MPI_Finalize();

  return result;
}
