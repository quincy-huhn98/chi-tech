#ifndef _chi_spds_h
#define _chi_spds_h

#include "ChiMesh/SweepUtilities/SPLS/SPLS.h"

//###################################################################
/**Contains multiple levels*/
struct chi_mesh::sweep_management::SPDS
{
  double                   polar;
  double                   azimuthal;
  chi_mesh::Vector         omega;

  chi_mesh::MeshContinuum* grid;

  SPLS*                    spls;
  std::vector<STDG*>       global_sweep_planes;  ///< Processor sweep planes
  std::vector<int>         location_dependencies;
  std::vector<int>         location_successors;
  std::vector<int>         delayed_location_dependencies;
  std::vector<int>         delayed_location_successors;

  std::vector<std::pair<int,int>> local_cyclic_dependencies;

  //======================================== Default constructor
  SPDS()
  {  }

  int MapLocJToPrelocI(int locJ);
  int MapLocJToDeplocI(int locJ);
  void AddLocalDependecy(int location_index);
  void AddLocalSuccessor(int location_index);
};

#endif