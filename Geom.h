#ifndef _GEOM_H
#define _GEOM_H

#include <CGAL/Cartesian.h>
#include <CGAL/Triangulation_vertex_base_with_info_2.h>
#include <CGAL/Delaunay_triangulation_2.h>

class DrawnFeature;

typedef CGAL::Cartesian<float>						K;
typedef CGAL::Triangulation_vertex_base_with_info_2<DrawnFeature *, K>	Vb;
typedef CGAL::Triangulation_face_base_2<K>				Fb;
typedef CGAL::Triangulation_data_structure_2<Vb,Fb>			Tds;
typedef CGAL::Delaunay_triangulation_2<K,Tds>				Triangulation;

typedef Triangulation::Point			Point;
typedef Triangulation::Locate_type		Locate_type;
typedef Triangulation::Face_handle		Face_handle;
typedef Triangulation::Vertex_handle		Vertex_handle;
typedef Triangulation::Vertex_circulator	Vertex_circulator;
typedef Triangulation::Finite_faces_iterator	Finite_faces_iterator;
typedef Triangulation::Finite_edges_iterator	Edge_iterator;
typedef K::Segment_2				Segment;

#endif	/* _GEOM_H */
