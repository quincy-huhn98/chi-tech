#include "lbs_linear_boltzman_solver.h"
#include <ChiMesh/Cell/cell.h>
#include <PiecewiseLinear/pwl.h>
#include <ChiPhysics/chi_physics.h>
#include <chi_log.h>

extern ChiLog chi_log;
extern ChiPhysics chi_physics_handler;

#include <iomanip>
#include "ChiConsole/chi_console.h"

extern ChiConsole chi_console;

//###################################################################
/**Initializes p_arrays.\n
The question arises of what datatype can store the total amount of
unknowns. For now we will say we want to be
designing for 100 billion cells with
an assumed shape of a truncated octahedron which has 24 vertices.
We will also assume that we will be able to do 2000 energy groups
and finally we will assume we will do scattering orders up to 16
which requires 289 moments.
   DOFS per truncated octahedron = 24\n
   Energy groups                 = 2000\n
   Moments                       = 289\n
   # of cells                    =   100,000,000,000\n
   Total DOFS                    = 2,400,000,000,000\n
   Unknowns per cell             =        13,872,000\n
   Total Unknowns                = A crap ton\n
\n
It is easy to see here that this is a hell of a lot so how about we think about
something more modest. Like 200 energy groups scattering order 5 (36 moments)
and 2 billion cells.\n
   Energy groups                 = 200\n
   Moments                       = 36\n
   # of cells                    =     2,000,000,000\n
   Total DOFS                    =    48,000,000,000\n
   Unknowns per cell             =             7,200\n
   Total Unknowns                = 1.44xe13\n
\n
A long int only supports up to 4.29e9. This obviously requires
unsigned long long int which can hold up to 2x2e63.\n
\n
Another interesting aspect is what it will take to get to exascale. For a
discrete ordinates code this will undoubtly be evident in the amount of angular flux
unknowns. 1 billion cells, 24 vertices, 200 groups, 48 azimuthal angles per
octant, 8 polar angles per octant (3072) angles. 1.47456e16. Just a factor 68
away from exascale.
   */
int LinearBoltzman::Solver::InitializeParrays()
{
  auto pwl_discretization = (SpatialDiscretization_PWL*)discretization;

  //================================================== Compute local # of dof
  local_dof_count=0;
  if (typeid(*discretization) == typeid(SpatialDiscretization_PWL))
  {
    size_t num_cell_views = pwl_discretization->cell_fe_views.size();
    for (int c=0; c<num_cell_views; c++)
      local_dof_count += pwl_discretization->cell_fe_views[c]->dofs;
  }
  chi_log.Log(LOG_ALLVERBOSE_2) << "Local DOF count = " << local_dof_count;

  //================================================== Compute global # of dof
  MPI_Allreduce(&local_dof_count,&glob_dof_count,1,
    MPI_UNSIGNED_LONG_LONG, MPI_SUM,MPI_COMM_WORLD);
  chi_log.Log(LOG_ALLVERBOSE_2) << "Global DOF count = " << glob_dof_count;

  //================================================== Compute num of unknowns
  int G = groups.size();
  int M = num_moments;
  unsigned long long local_unknown_count = local_dof_count*G*M;
  unsigned long long glob_unknown_count  = glob_dof_count*G*M;

  chi_log.Log(LOG_ALLVERBOSE_2)
    << "Local Unknown count = " << local_unknown_count;
  chi_log.Log(LOG_ALLVERBOSE_2)
    << "Globl Unknown count = " << glob_unknown_count;

  //================================================== Size local vectors
  q_moments_local.resize(local_unknown_count,0.0);
  phi_old_local.resize(local_unknown_count,0.0);
  phi_new_local.resize(local_unknown_count,0.0);

  //================================================== Read Restart data
  if (options.read_restart_data)
    ReadRestartData(options.read_restart_folder_name,
                    options.read_restart_file_base);
  MPI_Barrier(MPI_COMM_WORLD);

  //================================================== Initialize default
  //                                                   incident boundary
  typedef chi_mesh::sweep_management::BoundaryVacuum SweepVacuumBndry;
  typedef chi_mesh::sweep_management::BoundaryIncidentHomogenous SweepIncHomoBndry;
  typedef chi_mesh::sweep_management::BoundaryReflecting SweepReflectingBndry;
  std::vector<std::vector<double>>& flux_vec = incident_P0_mg_boundaries;

  // Defining default Vacuum boundary
  std::vector<double> zero_boundary(G,0.0);
  flux_vec.push_back(zero_boundary);

  // ================================================= Populate boundaries
  chi_mesh::Vector ihat(1.0,0.0,0.0);
  chi_mesh::Vector jhat(0.0,1.0,0.0);
  chi_mesh::Vector khat(0.0,0.0,1.0);
  int bndry_id=0;
  for (auto bndry_type : boundary_types)
  {
    int vec_index = bndry_type.second;

    if (bndry_type.first == LinearBoltzman::BoundaryType::VACUUM)
      sweep_boundaries.push_back(new SweepVacuumBndry(flux_vec.back()));
    else if (bndry_type.first == LinearBoltzman::BoundaryType::INCIDENT_ISOTROPIC)
      sweep_boundaries.push_back(new SweepIncHomoBndry(flux_vec[vec_index]));
    else if (bndry_type.first == LinearBoltzman::BoundaryType::REFLECTING)
    {
      chi_mesh::Normal normal;
      if (bndry_id == 0) normal = ihat;
      if (bndry_id == 1) normal = ihat*-1.0;
      if (bndry_id == 2) normal = jhat;
      if (bndry_id == 3) normal = jhat*-1.0;
      if (bndry_id == 4) normal = khat;
      if (bndry_id == 5) normal = khat*-1.0;

      sweep_boundaries.push_back(new SweepReflectingBndry(flux_vec.back(),
                                 normal));
    }

    ++bndry_id;
  }

  //================================================== Initialize transport views
  int num_grps = groups.size();
  int block_MG_counter = 0;       //Counts the strides of moment and group
  int block_counter = 0;          //Counts the base stride

  for (auto cell_g_index : grid->local_cell_glob_indices)
  {
    auto cell = grid->cells[cell_g_index];

    auto cell_fe_view =
      (CellFEView*)pwl_discretization->MapFeView(cell_g_index);
    auto full_cell_view =
      (LinearBoltzman::CellViewFull*)cell_transport_views[cell->cell_local_id];

    int mat_id = cell->material_id;

    full_cell_view->xs_id = matid_to_xs_map[mat_id];

    full_cell_view->dof_phi_map_start = block_MG_counter;
    block_MG_counter += cell_fe_view->dofs * num_grps * num_moments;

    //Init face upwind flags and adj_partition_id
    int num_faces = cell->faces.size();
    full_cell_view->face_f_upwind_flag.resize(num_faces,false);
    for (int f=0; f<num_faces; f++)
    {
      if (cell->faces[f].neighbor >= 0)
      {
        int adj_g_index = cell->faces[f].neighbor;
        auto adj_cell = grid->cells[adj_g_index];

        full_cell_view->face_f_adj_part_id.push_back(
          adj_cell->partition_id);
      }//if not bndry
      else
      {
        full_cell_view->face_f_adj_part_id.push_back(
          cell->faces[f].neighbor);

        chi_mesh::Vector& face_norm = cell->faces[f].normal;

        if      (face_norm.Dot(ihat)>0.999)
          full_cell_view->face_boundary_id.push_back(0);
        else if (face_norm.Dot(ihat)<-0.999)
          full_cell_view->face_boundary_id.push_back(1);
        else if (face_norm.Dot(jhat)>0.999)
          full_cell_view->face_boundary_id.push_back(2);
        else if (face_norm.Dot(jhat)<-0.999)
          full_cell_view->face_boundary_id.push_back(3);
        else if (face_norm.Dot(khat)>0.999)
          full_cell_view->face_boundary_id.push_back(4);
        else if (face_norm.Dot(khat)<-0.999)
          full_cell_view->face_boundary_id.push_back(5);

        cell->faces[f].neighbor = -(full_cell_view->face_boundary_id.back() + 1);
      }//if bndry
    }//for f

    //Add address
    local_cell_phi_dof_array_address.push_back(full_cell_view->dof_phi_map_start);
    local_cell_dof_array_address.push_back(block_counter);
    block_counter += cell_fe_view->dofs;
  }//for local cell

  //================================================== Initialize Field Functions
  for (int g=0; g<groups.size(); g++)
  {
    for (int m=0; m<num_moments; m++)
    {

      std::string text_name = std::string("Flux_g") +
                            std::to_string(g) +
                            std::string("_m") + std::to_string(m);

      auto group_ff = new chi_physics::FieldFunction(
          text_name,                                    //Text name
          chi_physics_handler.fieldfunc_stack.size(),   //FF-id
          chi_physics::FieldFunctionType::DFEM_PWL,     //Type
          grid,                                         //Grid
          discretization,                               //Spatial Discretization
          groups.size(),                                //Number of components
          num_moments,                                  //Number of sets
          g,m,                                          //Ref component, ref set
          &local_cell_phi_dof_array_address,            //Dof block address
          &phi_old_local);                              //Data vector

      chi_physics_handler.fieldfunc_stack.push_back(group_ff);
      field_functions.push_back(group_ff);
    }//for m
  }//for g




  return 0;
}