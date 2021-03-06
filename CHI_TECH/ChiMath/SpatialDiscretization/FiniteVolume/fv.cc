#include "fv.h"

#include <ChiMesh/MeshContinuum/chi_meshcontinuum.h>
#include <ChiMesh/Cell/cell_slab.h>
#include <ChiMesh/Cell/cell_polygon.h>
#include <ChiMesh/Cell/cell_polyhedron.h>

#include "CellViews/fv_slab.h"
#include "CellViews/fv_polygon.h"
#include "CellViews/fv_polyhedron.h"

#include <chi_log.h>

extern ChiLog chi_log;

//###################################################################
/**Only constructor for this method.*/
SpatialDiscretization_FV::SpatialDiscretization_FV(int dim)
  : SpatialDiscretization(dim)
{
  mapping_initialized = false;
}

//###################################################################
/**Adds a PWL Finite Element for each cell of the local problem.*/
void SpatialDiscretization_FV::AddViewOfLocalContinuum(
  chi_mesh::MeshContinuum* vol_continuum,
  int num_cells,
  int* cell_indices)
{
  //================================================== Create empty view
  //                                                 for each cell
  if (!mapping_initialized)
  {
    this->cell_fv_views_mapping.reserve(vol_continuum->cells.size());
    std::vector<chi_mesh::Cell*>::iterator cellit;
    for (cellit = vol_continuum->cells.begin();
         cellit != vol_continuum->cells.end();
         cellit++)
    {
      this->cell_fv_views_mapping.push_back(-1);
    }
    mapping_initialized = true;
  }


  //================================================== Swap views for
  //                                                   specified item_id
  int cell_index = -1;
  for (int c=0; c<num_cells; c++)
  {
    cell_index = cell_indices[c];
    chi_mesh::Cell* cell = vol_continuum->cells[cell_index];

    if (cell_fv_views_mapping[cell_index]<0)
    {
      //######################################### SLAB
      if (cell->Type() == chi_mesh::CellType::SLAB)
      {
        auto view =
          new SlabFVView((chi_mesh::CellSlab*)cell, vol_continuum);

        this->cell_fv_views.push_back(view);
        cell_fv_views_mapping[cell_index] = this->cell_fv_views.size()-1;
      }

      //######################################### POLYGON
      if (cell->Type() == chi_mesh::CellType::POLYGON)
      {
        auto view =
          new PolygonFVView((chi_mesh::CellPolygon*)(cell), vol_continuum);

        this->cell_fv_views.push_back(view);
        cell_fv_views_mapping[cell_index] = this->cell_fv_views.size()-1;
      }

      //######################################### POLYHEDRON
      if (cell->Type() == chi_mesh::CellType::POLYHEDRON)
      {
        auto view =
          new PolyhedronFVView(
            (chi_mesh::CellPolyhedron*)(cell),
            vol_continuum);

        this->cell_fv_views.push_back(view);
        cell_fv_views_mapping[cell_index] = this->cell_fv_views.size()-1;
      }
    }//if mapping not yet assigned
  }//for num cells



}//AddViewOfLocalContinuum

//###################################################################
/**Adds a PWL Finite Element for each cell of the local problem.*/
void SpatialDiscretization_FV::AddViewOfLocalContinuum(
  chi_mesh::MeshContinuum* vol_continuum)
{
  //================================================== Create empty view
  //                                                 for each cell
  if (!mapping_initialized)
  {
    this->cell_fv_views_mapping.reserve(vol_continuum->cells.size());
    std::vector<chi_mesh::Cell*>::iterator cellit;
    for (cellit = vol_continuum->cells.begin();
         cellit != vol_continuum->cells.end();
         cellit++)
    {
      this->cell_fv_views_mapping.push_back(-1);
    }
    mapping_initialized = true;
  }


  //================================================== Swap views for
  //                                                   specified item_id
  for (auto& cell_index : vol_continuum->local_cell_glob_indices)
  {
    chi_mesh::Cell* cell = vol_continuum->cells[cell_index];

    if (cell_fv_views_mapping[cell_index]<0)
    {
      //######################################### SLAB
      if (cell->Type() == chi_mesh::CellType::SLAB)
      {
        auto view =
          new SlabFVView((chi_mesh::CellSlab*)cell, vol_continuum);

        this->cell_fv_views.push_back(view);
        cell_fv_views_mapping[cell_index] = this->cell_fv_views.size()-1;
      }

      //######################################### POLYGON
      if (cell->Type() == chi_mesh::CellType::POLYGON)
      {
        auto view =
          new PolygonFVView((chi_mesh::CellPolygon*)(cell), vol_continuum);

        this->cell_fv_views.push_back(view);
        cell_fv_views_mapping[cell_index] = this->cell_fv_views.size()-1;
      }

      //######################################### POLYHEDRON
      if (cell->Type() == chi_mesh::CellType::POLYHEDRON)
      {
        auto view =
          new PolyhedronFVView(
            (chi_mesh::CellPolyhedron*)(cell),
            vol_continuum);

        this->cell_fv_views.push_back(view);
        cell_fv_views_mapping[cell_index] = this->cell_fv_views.size()-1;
      }
    }//if mapping not yet assigned
  }//for num cells



}//AddViewOfLocalContinuum


//###################################################################
/**Maps the cell index to a position stored locally.*/
CellFVView* SpatialDiscretization_FV::MapFeView(int cell_glob_index)
{
  CellFVView* value = cell_fv_views.at(cell_fv_views_mapping[cell_glob_index]);
  return value;
}