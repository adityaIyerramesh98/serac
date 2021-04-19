#include "mfem.hpp"
#include "parvariationalform.hpp"
#include "qfuncintegrator.hpp"
#include "tensor.hpp"
#include <fstream>
#include <iostream>

#include "serac/serac_config.hpp"

using namespace std;
using namespace mfem;

int main(int argc, char* argv[])
{
  int num_procs, myid;
  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);

  const char* mesh_file = SERAC_REPO_DIR "/data/meshes/star.mesh";

  int    order       = 1;
  int    refinements = 0;
  double a           = 1.0;
  double b           = 1.0;
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

  int dim = mesh.Dimension();
  {
    for (int l = 0; l < refinements; l++) {
      mesh.UniformRefinement();
    }
  }

  ParMesh pmesh(MPI_COMM_WORLD, mesh);

  auto                  fec = H1_FECollection(order, dim);
  ParFiniteElementSpace fespace(&pmesh, &fec);

  Array<int> ess_bdr(pmesh.bdr_attributes.Max());
  ess_bdr = 1;

  FunctionCoefficient boundary_func([&](const Vector& coords) {
    double x = coords(0);
    double y = coords(1);
    return 1 + x + 2 * y;
  });

  ParGridFunction x(&fespace);
  x = 0.0;

  x.ProjectBdrCoefficient(boundary_func, ess_bdr);

  ParVariationalForm form(&fespace);

  auto tmp = new QFunctionIntegrator(
      [&](auto x, auto u, auto du) {
        auto f0 = a * u - (100 * x[0] * x[1]);
        auto f1 = b * du;
        return std::tuple{f0, f1};
      },
      pmesh);

  form.AddDomainIntegrator(tmp);

  form.SetEssentialBC(ess_bdr);

  CGSolver cg(MPI_COMM_WORLD);
  cg.SetRelTol(1e-6);
  cg.SetMaxIter(2000);
  cg.SetPrintLevel(1);
  cg.iterative_mode = 0;

  NewtonSolver newton(MPI_COMM_WORLD);
  newton.SetOperator(form);
  newton.SetSolver(cg);
  newton.SetPrintLevel(1);
  newton.SetRelTol(1e-8);
  newton.SetMaxIter(100);

  Vector zero;
  Vector X;
  x.GetTrueDofs(X);
  newton.Mult(zero, X);

  x.Distribute(X);

  mfem::ConstantCoefficient zero_coef(0.0);
  std::cout << x.ComputeL2Error(zero_coef) << std::endl;
  std::cout << "expected: 0.873569 (with \"-r 2\")" << std::endl;

  // x.ProjectCoefficient(u_excoeff);

  char         vishost[] = "localhost";
  int          visport   = 19916;
  socketstream sol_sock(vishost, visport);
  sol_sock << "parallel " << num_procs << " " << myid << "\n";
  sol_sock.precision(8);
  sol_sock << "solution\n" << pmesh << x << flush;

  MPI_Finalize();

  return 0;
}
