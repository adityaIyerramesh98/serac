// Copyright (c) 2019, Lawrence Livermore National Security, LLC and
// other Serac Project Developers. See the top-level LICENSE file for
// details.
//
// SPDX-License-Identifier: (BSD-3-Clause) 

#include <gtest/gtest.h>

#include "mfem.hpp"
#include "solvers/dynamic_solver.hpp"
#include <fstream>

using namespace std;
using namespace mfem;

void InitialDeformation(const Vector &x, Vector &y);

void InitialVelocity(const Vector &x, Vector &v);

TEST(dynamic_solver, dyn_solve)
{
   MPI_Barrier(MPI_COMM_WORLD);

   // mesh
   const char *mesh_file = "../../data/beam-hex.mesh";

   // Open the mesh
   ifstream imesh(mesh_file);
   Mesh* mesh = new Mesh(imesh, 1, 1, true);
   imesh.close();

   // declare pointer to parallel mesh object
   ParMesh *pmesh = NULL;
   mesh->UniformRefinement();
   
   pmesh = new ParMesh(MPI_COMM_WORLD, *mesh);
   delete mesh;

   int dim = pmesh->Dimension();

   ODESolver *ode_solver = new SDIRK33Solver;
   
   // Define the finite element spaces for displacement field
   H1_FECollection fe_coll(1, dim);
   ParFiniteElementSpace fe_space(pmesh, &fe_coll, dim);

   int true_size = fe_space.TrueVSize();
   Array<int> true_offset(3);
   true_offset[0] = 0;
   true_offset[1] = true_size;
   true_offset[2] = 2*true_size;

   BlockVector vx(true_offset);
   ParGridFunction v_gf, x_gf;
   v_gf.MakeTRef(&fe_space, vx, true_offset[0]);
   x_gf.MakeTRef(&fe_space, vx, true_offset[1]);

   VectorFunctionCoefficient velo_coef(dim, InitialVelocity);   
   v_gf.ProjectCoefficient(velo_coef);
   v_gf.SetTrueVector();

   VectorFunctionCoefficient deform(dim, InitialDeformation);
   x_gf.ProjectCoefficient(deform);
   x_gf.SetTrueVector();
   
   v_gf.SetFromTrueVector(); x_gf.SetFromTrueVector();

   
   // define a boundary attribute array and initialize to 0
   Array<int> ess_bdr;
   ess_bdr.SetSize(fe_space.GetMesh()->bdr_attributes.Max());
   ess_bdr = 0;

   // boundary attribute 1 (index 0) is fixed (Dirichlet)
   ess_bdr[0] = 1;
   
      
   // construct the nonlinear mechanics operator
   DynamicSolver oper(fe_space, ess_bdr,
                      0.25, 5.0, 0.0,
                      1.0e-4, 1.0e-8, 
                      500, true, false);

   double t = 0.0;
   double t_final = 6.0;
   double dt = 3.0;
   
   oper.SetTime(t);
   ode_solver->Init(oper);

   // 10. Perform time-integration
   //     (looping over the time iterations, ti, with a time-step dt).
   bool last_step = false;
   for (int ti = 1; !last_step; ti++) {
      double dt_real = min(dt, t_final - t);
      
      ode_solver->Step(vx, t, dt_real);

      last_step = (t >= t_final - 1e-8*dt);
   }
   
   {
      v_gf.SetFromTrueVector(); x_gf.SetFromTrueVector();
      GridFunction *nodes = &x_gf;
      int owns_nodes = 0;
      pmesh->SwapNodes(nodes, owns_nodes);

      int myid;
      MPI_Comm_rank(MPI_COMM_WORLD, &myid);
      
      ostringstream mesh_name, velo_name, ee_name;
      mesh_name << "deformed." << setfill('0') << setw(6) << myid;
      velo_name << "velocity." << setfill('0') << setw(6) << myid;

      ofstream mesh_ofs(mesh_name.str().c_str());
      mesh_ofs.precision(8);
      pmesh->Print(mesh_ofs);
      pmesh->SwapNodes(nodes, owns_nodes);
      ofstream velo_ofs(velo_name.str().c_str());
      velo_ofs.precision(8);
      v_gf.Save(velo_ofs);
   }
   
   delete pmesh;
   
   MPI_Barrier(MPI_COMM_WORLD);
}


int main(int argc, char* argv[])
{
  int result = 0;

  ::testing::InitGoogleTest(&argc, argv);

  MPI_Init(&argc, &argv);
  result = RUN_ALL_TESTS();
  MPI_Finalize();

  return result;
}

void InitialDeformation(const Vector &x, Vector &y)
{
   // set the initial configuration to be the same as the reference, stress
   // free, configuration
   y = x;
}

void InitialVelocity(const Vector &x, Vector &v)
{
   const int dim = x.Size();
   const double s = 0.1/64.;

   v = 0.0;
   v(dim-1) = s*x(0)*x(0)*(8.0-x(0));
   v(0) = -s*x(0)*x(0);
}
