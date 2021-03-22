#include "mfem.hpp"
#include "parvariationalform.hpp"
#include "l2qfuncintegrator.hpp"
#include "tensor.hpp"
#include <fstream>
#include <iostream>

#include "serac/serac_config.hpp"
#include "serac/physics/operators/stdfunction_operator.hpp"
#include "serac/numerics/expr_template_ops.hpp"

using namespace std;
using namespace mfem;

#include "axom/slic/core/SimpleLogger.hpp"

int main(int argc, char* argv[])
{
  int num_procs, myid;
  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);

  axom::slic::SimpleLogger logger;

  const char * mesh_file = SERAC_REPO_DIR"/data/meshes/star.mesh";

  int         order       = 1;
  int         refinements = 0;
  // SERAC EDIT BEGIN
  // double p = 5.0;
  // SERAC EDIT END

  OptionsParser args(argc, argv);
  args.AddOption(&mesh_file, "-m", "--mesh", "Mesh file to use.");
  args.AddOption(&refinements, "-r", "--ref", "");
  args.AddOption(&order, "-o", "--order", "");

  args.Parse();
  if (!args.Good()) {
    if (myid == 0) {
      args.PrintUsage(cout);
    }
    MPI_Finalize();
    return 1;
  }
  if (myid == 0) {
    args.PrintOptions(cout);
  }

  Mesh mesh(mesh_file, 1, 1);
  for (int l = 0; l < refinements; l++) {
    mesh.UniformRefinement();
  }

  ParMesh pmesh(MPI_COMM_WORLD, mesh);

  auto fec = L2_FECollection(order, pmesh.Dimension());
  ParFiniteElementSpace fespace(&pmesh, &fec);

  ParBilinearForm A(&fespace);

  ConstantCoefficient coef(1.0);
  A.AddDomainIntegrator(new MassIntegrator(coef));

  A.Assemble(0);
  A.Finalize();
  std::unique_ptr<mfem::HypreParMatrix> J(A.ParallelAssemble());

  LinearForm f(&fespace);
  FunctionCoefficient load_func([&](const Vector& coords) {
    return 100 * coords(0) * coords(1);
  });

  f.AddDomainIntegrator(new DomainLFIntegrator(load_func));
  f.Assemble();

  FunctionCoefficient boundary_func([&](const Vector& coords) {
    double x = coords(0);
    double y = coords(1);
    return 1 + x + 2 * y;
  });

  ParGridFunction x(&fespace);
  x = 0.0;
  auto residual = serac::mfem_ext::StdFunctionOperator(
    fespace.TrueVSize(),

    [&](const mfem::Vector& u, mfem::Vector& r) {
      r = A * u - f;
    },

    [&](const mfem::Vector & /*du_dt*/) -> mfem::Operator& {
      return *J;
    }
  );

  CGSolver cg(MPI_COMM_WORLD);
  cg.SetRelTol(1e-10);
  cg.SetMaxIter(2000);
  cg.SetPrintLevel(1);

  cg.iterative_mode = 0;

  NewtonSolver newton(MPI_COMM_WORLD);
  newton.SetOperator(residual);
  newton.SetSolver(cg);
  newton.SetPrintLevel(1);
  newton.SetRelTol(1e-8);
  newton.SetMaxIter(100);

  Vector zero;
  Vector X(fespace.TrueVSize());

  x.GetTrueDofs(X);
  newton.Mult(zero, X);

  x.Distribute(X);

  ParVariationalForm form(&fespace);

  auto tmp = new L2QFunctionIntegrator(
      [&](auto x, auto u) {
        return u - (100 * x[0] * x[1]);
      }, pmesh);

  form.AddDomainIntegrator(tmp);

  ParGridFunction x2(&fespace);
  Vector X2(fespace.TrueVSize());
  x2 = 0.0;

  newton.SetOperator(form);

  x2.GetTrueDofs(X2);
  newton.Mult(zero, X2);

  x2.Distribute(X2);

  mfem::ConstantCoefficient zero_coef(0.0);
  std::cout << "relative error: " << mfem::Vector(x - x2).Norml2() / x.Norml2() << std::endl;
  std::cout << x.ComputeL2Error(zero_coef) << std::endl;
  std::cout << x2.ComputeL2Error(zero_coef) << std::endl;

  char         vishost[] = "localhost";
  int          visport   = 19916;
  socketstream sol_sock(vishost, visport);
  sol_sock << "parallel " << num_procs << " " << myid << "\n";
  sol_sock.precision(8);
  sol_sock << "solution\n" << pmesh << x << flush;

  socketstream sol_sock2(vishost, visport);
  sol_sock2 << "parallel " << num_procs << " " << myid << "\n";
  sol_sock2.precision(8);
  sol_sock2 << "solution\n" << pmesh << x2 << flush;

  MPI_Finalize();

  return 0;
}
