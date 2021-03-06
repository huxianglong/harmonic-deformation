#include <igl/readOFF.h>
#include <igl/opengl/glfw/Viewer.h>
#include <igl/slice_into.h>
#include <igl/rotate_by_quat.h>
#include <igl/opengl/glfw/imgui/ImGuiMenu.h>
#include <igl/opengl/glfw/imgui/ImGuiHelpers.h>
#include <sstream>
#include <Eigen/Core>
#include <igl/boundary_facets.h>
#include <igl/colon.h>
#include <igl/cotmatrix.h>
#include <igl/jet.h>
#include <igl/min_quad_with_fixed.h>
#include <igl/readOFF.h>
#include <igl/setdiff.h>
#include <igl/slice.h>
#include <igl/slice_into.h>
#include <igl/unique.h>
#include <igl/opengl/glfw/Viewer.h>
#include <Eigen/Sparse>
#include <igl/triangle/triangulate.h>
#include <vector>
#include <iostream>

#include "Lasso.h"
#include "Colors.h"
#include "igl/readOFF.h"
#include "Eigen/Dense"
#include "Eigen/Core"
#include "igl/unproject.h"

//activate this for alternate UI (easier to debug)
//#define UPDATE_ONLY_ON_UP

using namespace std;
using namespace Eigen;
using namespace igl;

bool ifdebug = false;
bool ifreduce = true;
bool freeze = false;
bool retri = false;
//vertex array, #V x3
Eigen::MatrixXd V(0, 3);
//harmonic coordinates in three dimension, stands for x, y, z respectively, #V x1
Eigen::MatrixXd X(0, 3);
Eigen::MatrixXd Y(0, 3);
Eigen::MatrixXd Z(0, 3);
//cage vertex array, #V x3
Eigen::MatrixXd VC(0, 3);
Eigen::MatrixXd Vn;
// matrix used to draw cage edge
VectorXd R1, R2;
MatrixXd E_bat, V_bat;
VectorXd b_bat;
//cage edge
Eigen::MatrixXd C1, C2;
//face array, #F x3
Eigen::MatrixXi F(0, 3);
// inner face
Eigen::MatrixXi FI(0, 3);

//mouse interaction
enum MouseMode {
    SELECT, TRANSLATE, ROTATE, NONE
};
MouseMode mouse_mode = NONE;
bool doit = false;
int down_mouse_x = -1, down_mouse_y = -1;

//for selecting vertices
std::unique_ptr<Lasso> lasso;
//list of currently selected vertices
Eigen::VectorXi selected_v(0, 1);

//for saving constrained vertices
//vertex-to-handle index, #V x1 (-1 if vertex is free)
Eigen::VectorXi handle_id(0, 1);
//list of all vertices belonging to handles, #HV x1
Eigen::VectorXi handle_vertices(0, 1);
//centroids of handle regions, #H x1
Eigen::MatrixXd handle_centroids(0, 3);
//updated positions of handle vertices, #HV x3
Eigen::MatrixXd handle_vertex_positions(0, 3);
//index of handle being moved
int moving_handle = -1;
//rotation and translation for the handle being moved
Eigen::Vector3f translation(0, 0, 0);
Eigen::Vector4f rotation(0, 0, 0, 1.);

//per vertex color array, #V x3
Eigen::MatrixXd vertex_colors;

//function declarations (see below for implementation)
bool solve(igl::opengl::glfw::Viewer &viewer);

void get_new_handle_locations();

Eigen::Vector3f computeTranslation(igl::opengl::glfw::Viewer &viewer, int mouse_x, int from_x, int mouse_y, int from_y,
                                   Eigen::RowVector3d pt3D);

Eigen::Vector4f computeRotation(igl::opengl::glfw::Viewer &viewer, int mouse_x, int from_x, int mouse_y, int from_y,
                                Eigen::RowVector3d pt3D);

void compute_handle_centroids();

Eigen::MatrixXd readMatrix(const char *filename);

bool callback_mouse_down(igl::opengl::glfw::Viewer &viewer, int button, int modifier);

bool callback_mouse_move(igl::opengl::glfw::Viewer &viewer, int mouse_x, int mouse_y);

bool callback_mouse_up(igl::opengl::glfw::Viewer &viewer, int button, int modifier);

bool callback_pre_draw(igl::opengl::glfw::Viewer &viewer);

bool callback_pre_draw_point(igl::opengl::glfw::Viewer &viewer);

bool callback_key_down(igl::opengl::glfw::Viewer &viewer, unsigned char key, int modifiers);

void onNewHandleID();

void applySelection();

Eigen::MatrixXi E;
Eigen::VectorXi b, IA, IC;
Eigen::VectorXi all, in;
Eigen::SparseMatrix<double> L, L_in_in, L_in_b;


double orientation(Eigen::RowVector2d &p, Eigen::RowVector2d &q, Eigen::RowVector2d &r) {
    double val = (q(1) - p(1)) * (r(0) - q(0)) - (q(0) - p(0)) * (r(1) - q(1));
    return val;
}

void init_handle() {
    using namespace Eigen;
    // Find boundary edges
    igl::boundary_facets(F, E);
    // Find boundary vertices
    if (ifdebug) {
        b.resize(4);
        b << 0, 1, 2, 3;
    } else igl::unique(E, b, IA, IC);
    if (ifdebug)
        cout << "b is " << b << endl;
    // List of all vertex indices
    igl::colon<int>(0, V.rows() - 1, all);
    // List of interior indices
    igl::setdiff(all, b, in, IA);
    if (ifdebug) cout << "in is" << in << endl;
    // Construct and slice up Laplacian
    igl::cotmatrix(V, F, L);
    igl::slice(L, in, in, L_in_in);
    igl::slice(L, in, b, L_in_b);

    // init handle
    handle_id.setConstant(V.rows(), 1, -1);
    for (int i = 0; i < b.rows(); ++i) {
        handle_id[b[i]] = i;
    }
    onNewHandleID();
    get_new_handle_locations();
    // slice VC
    igl::slice(V, b, 1, VC);
    // get new FI
    if (!ifreduce) {
        FI.resize(2, 3);
        FI << 4, 6, 5, 5, 6, 7;
    } else FI = F;
}

void retriangulate(igl::opengl::glfw::Viewer &viewer) {
//    igl::slice_into(V_bat, b_bat, 1, VC);
    for (int i = 0; i < b_bat.rows(); ++i) {
        V_bat(b_bat(i), 0) = V(b_bat(i), 0);
        V_bat(b_bat(i), 1) = V(b_bat(i), 1);
        if (ifdebug) cout << V.row(b_bat(i)) << endl;
    }
    MatrixXd Ht, Vn;
    igl::triangle::triangulate(V_bat, E_bat, Ht, "a0.005q", Vn, F);
    viewer.data().clear();
    V = Vn;
    V.conservativeResize(V.rows(), 3);
    V.col(2) = VectorXd::Zero(V.rows());
    //clear points and lines
    viewer.data().set_points(Eigen::MatrixXd::Zero(0, 3), Eigen::MatrixXd::Zero(0, 3));
    viewer.data().set_edges(Eigen::MatrixXd::Zero(0, 3), Eigen::MatrixXi::Zero(0, 3), Eigen::MatrixXd::Zero(0, 3));
    MatrixXd B1, B2, C1, C2;
    slice(V_bat, b_bat, 1, B1);
    viewer.data().set_points(B1, Eigen::RowVector3d(1, 0, 0));
    if (ifdebug) cout << B1 << endl;
    viewer.data().set_mesh(V, F);
    init_handle();
}

void convex_hull(Eigen::MatrixXd &V, Eigen::MatrixXi &F, Eigen::MatrixXd Vn) {
    using namespace std::chrono;
    MatrixXd E, B;
    // adjust the size of the cage
    double factor = 1.5;
    // Initialize Result
    int n = V.rows();
    vector<int> hull;
    // computer the centroid and translate it
    RowVector2d e;
    e << 0, 0;
    for (int i = 0; i < V.rows(); ++i) e += V.row(i);
    e(0) /= n;
    e(1) /= n;
    for (int i = 0; i < V.rows(); ++i) V.row(i) -= e;

    // Find the leftmost point
    int l = 0;
    for (int i = 1; i < V.rows(); i++)
        if (V(i, 0) < V(l, 0))
            l = i;

    // Start from leftmost point, keep moving counterclockwise
    // until reach the start point again.  This loop runs O(h)
    // times where h is number of points in result or output.
    int p = l, q;
    auto start = high_resolution_clock::now();
    auto stop = high_resolution_clock::now();
    auto duration = duration_cast<seconds>(stop - start);
    int cnt = 0;
    Eigen::RowVector2d pv, qv, iv;
    // visualize the result
    igl::opengl::glfw::Viewer viewer;
    viewer.data().clear();
    viewer.data().point_size = 10;
    do {
        // Add current point to result
        hull.push_back(p);
        pv = V.row(p);
        viewer.data().add_points(pv, Eigen::RowVector3d(1, 0, 0));
        cnt++;
        q = (p + 1) % n;
        qv = V.row(q);
        for (int i = 0; i < n; i++) {
            // If i is more counterclockwise than current q, then update q
            iv = V.row(i);
            double val = orientation(pv, iv, qv);
            if (val > 1e-10) {
                q = i;
                qv = V.row(q);
            }
        }
        stop = high_resolution_clock::now();
        duration = duration_cast<seconds>(stop - start);
        // if there are too many points, print them
        if (cnt % 100 == 0 & ifdebug) { cout << cnt << "boundary taks:" << duration.count() << endl; }
        p = q;
    } while (p != l);

    // get the boundary B and draw them with b_temp
    B.resize(hull.size(), 2);
    // add enlarged boundary to V
    V.conservativeResize(n + B.rows(), V.cols());
    MatrixXd B_temp;
    B_temp.resize(hull.size(), 2);
    for (int i = 0; i < hull.size(); ++i) {
        B.row(i) = V.row(hull[i]);
        for (int j = 0; j < B.cols(); ++j)
            B(i, j) *= factor;
        V.row(n + i) = B.row(i);
        if (i > 1)
            B_temp.row(i) = B.row(i - 1);
    }
    B_temp.row(0) = B.row(hull.size() - 1);
    // get the edge
    E.resize(hull.size(), 2);
    for (int i = 0; i <= hull.size() - 2; ++i) {
        E(i, 0) = i + n;
        E(i, 1) = i + 1 + n;
    }
    E(hull.size() - 1, 0) = hull.size() - 1 + n;
    E(hull.size() - 1, 1) = n;
    // get the triangle
    MatrixXd Ht;
    igl::triangle::triangulate(V, E, Ht, "a0.005q", Vn, F);
    V_bat = V.replicate(1, 1);
    E_bat = E.replicate(1, 1);
    b_bat = E.col(0);
    // visualize the convex hull
//    if (ifdebug) {
    viewer.data().add_edges(B, B_temp, Eigen::RowVector3d(1, 0, 0));
    viewer.data().set_mesh(Vn, F);
    viewer.launch();
//    }
    V = Vn;
}

void Reduction(Eigen::MatrixXd &V, Eigen::MatrixXi &F, int dim) {
    // parameter of cage
    double factor = 1.2;
    // reduce one dimension of V
    VectorXd x;
    x.setZero(V.rows());
    // reduce V to 2d
    if (dim == 0) {
        V.col(0) = V.col(2);
        V.col(2) = x;
    }
    if (dim == 1) {
        V.col(1) = V.col(2);
        V.col(2) = x;
    }
    if (dim == 2) {
        V.col(2) = x;
    }
    NoChange_t change = NoChange;
    V.conservativeResize(change, 2);
    // find the inner boundary of reduced point cloud
    convex_hull(V, F, Vn);
    V.conservativeResize(change, 3);
    V.col(2) = RowVectorXd::Zero(V.rows());
}

void solve_scalar(int dim, Eigen::VectorXd &Z) {
    using namespace Eigen;
    // Dirichlet boundary conditions from z-coordinate
    VectorXd bc;
    Z = V.col(dim);
    igl::slice(Z, b, bc);

    // Solve PDE
    SimplicialLLT<SparseMatrix<double> > solver(-L_in_in);
    //cout << L_in_in << endl;
    VectorXd Z_in = solver.solve(L_in_b * bc);
    //cout << L_in_b << endl;
    // slice into solution
    igl::slice_into(Z_in, in, Z);
    if (ifdebug) cout << Z_in << endl;
}

bool solve(igl::opengl::glfw::Viewer &viewer) {
    /* code for computing the deformation from handle_vertex_positions and handle_vertices here (replace following line) */
    igl::slice_into(handle_vertex_positions, handle_vertices, 1, V);
    Eigen::VectorXd Z;
    if (!freeze) {
        for (int i = 0; i < V.cols(); ++i) {
            Z = V.col(i);
            solve_scalar(i, Z);
            V.col(i) = Z;
        }
    }
    return true;
};

void get_new_handle_locations() {
    int count = 0;
    for (long vi = 0; vi < V.rows(); ++vi)
        if (handle_id[vi] >= 0) {
            Eigen::RowVector3f goalPosition = V.row(vi).cast<float>();
            if (handle_id[vi] == moving_handle) {
                if (mouse_mode == TRANSLATE)
                    goalPosition += translation;
                else if (mouse_mode == ROTATE) {
                    goalPosition -= handle_centroids.row(moving_handle).cast<float>();
                    igl::rotate_by_quat(goalPosition.data(), rotation.data(), goalPosition.data());
                    goalPosition += handle_centroids.row(moving_handle).cast<float>();
                }
            }
            handle_vertex_positions.row(count++) = goalPosition.cast<double>();
        }
}


void init_cage() {
    R1.setLinSpaced(VC.rows(), 0, VC.rows() - 1);
    R2.setLinSpaced(VC.rows(), 1, VC.rows());
    R2(VC.rows() - 1) = 0;
}

void update_cage() {
    slice(VC, R1, 1, C1);
    slice(VC, R2, 1, C2);
    if (ifdebug) cout << "C1: " << C1 << endl;
    if (ifdebug) cout << "C2: " << C2 << endl;
}

void default_handle(igl::opengl::glfw::Viewer &viewer) {
    handle_id.setConstant(V.rows(), 1, -1);
    for (int i = 0; i < b_bat.rows(); ++i) {
        handle_id[b_bat[i]] = i;
    }
    MatrixXd B1;
    slice(V, b_bat, 1, B1);
    viewer.data().set_points(Eigen::MatrixXd::Zero(0, 3), Eigen::MatrixXd::Zero(0, 3));
    viewer.data().set_points(B1, Eigen::RowVector3d(1, 0, 0));
    onNewHandleID();
    get_new_handle_locations();
}


int main(int argc, char *argv[]) {
    // Read mesh
    if (argc != 4) {
        cout << "Usage assignment5_bin ifreduce(1/0) dim(0/1/2) ../data/bunny.off" << endl;
        exit(0);
    }
    istringstream ss(argv[1]);
    int x;
    if (!(ss >> x))
        cerr << "Invalid reduce, must be interget " << argv[1] << '\n';
    if (x == 1)
        ifreduce = true;
    else ifreduce = false;
    if (ifreduce) {
        istringstream ss(argv[2]);
        int x;
        if (!(ss >> x))
            cerr << "Invalid dimension, must be integer " << argv[1] << '\n';
        std::exception_ptr eptr;
        try {
            igl::readOFF(argv[3], V, F);
        }
        catch (...) {
            eptr = std::current_exception(); // capture
        }
        igl::readOBJ(argv[3], V, F);

        Reduction(V, F, x);
    } else igl::readOFF("../data/cube_2d.off", V, F);
    // get the cage
    init_handle();
    // Plot the mesh
    igl::opengl::glfw::Viewer viewer;
    viewer.data().clear();
    viewer.data().set_mesh(V, FI);
    // draw cage
    init_cage();

    igl::opengl::glfw::imgui::ImGuiMenu menu;
    viewer.plugins.push_back(&menu);
    viewer.callback_key_down = callback_key_down;
    // Add content to the default menu window
    menu.callback_draw_viewer_menu = [&]() {
        // Draw parent menu content
        menu.draw_viewer_menu();

        // Add new group
        if (ImGui::CollapsingHeader("Deformation Controls", ImGuiTreeNodeFlags_DefaultOpen)) {

            // Expose an enumeration type
            ImGui::Combo("MouseMode", (int *) (&mouse_mode), "SELECT\0TRANSLATE\0ROTATE\0NONE\0\0");

            // Add a button
            if (ImGui::Button("ApplySelection")) {
                applySelection();
            }
            if (ImGui::Button("ClearConstraints")) {
                handle_id.setConstant(V.rows(), 1, -1);
            }
            if (ImGui::Button("Default Handle")) {
                default_handle(viewer);
                viewer.data().set_points(Eigen::MatrixXd::Zero(0, 3), Eigen::MatrixXd::Zero(0, 3));
                viewer.data().set_edges(Eigen::MatrixXd::Zero(0, 3), Eigen::MatrixXi::Zero(0, 3), Eigen::MatrixXd::Zero(0, 3));
            }
            if (ImGui::Button("Retriangulate")) {
                retriangulate(viewer);
//                retriangulate();
            }
            ImGui::Checkbox("Freeze", &freeze);
            ImGui::Checkbox("Retri", &retri);
        }
    };

    viewer.callback_mouse_down = callback_mouse_down;
    viewer.callback_mouse_move = callback_mouse_move;
    viewer.callback_mouse_up = callback_mouse_up;
    viewer.callback_pre_draw = callback_pre_draw_point;

    // Initialize selector
    lasso = std::unique_ptr<Lasso>(new Lasso(V, F, viewer));
    viewer.core.set_rotation_type(igl::opengl::ViewerCore::ROTATION_TYPE_TRACKBALL);
    viewer.launch();
}


bool callback_mouse_down(igl::opengl::glfw::Viewer &viewer, int button, int modifier) {
    if (button == (int) igl::opengl::glfw::Viewer::MouseButton::Right)
        return false;

    down_mouse_x = viewer.current_mouse_x;
    down_mouse_y = viewer.current_mouse_y;

    if (mouse_mode == SELECT) {
        if (lasso->strokeAdd(viewer.current_mouse_x, viewer.current_mouse_y) >= 0)
            doit = true;
        else
            lasso->strokeReset();
    } else if ((mouse_mode == TRANSLATE) || (mouse_mode == ROTATE)) {
        int vi = lasso->pickVertex(viewer.current_mouse_x, viewer.current_mouse_y);
        if (vi >= 0 && handle_id[vi] >= 0)  //if a region was found, mark it for translation/rotation
        {
            moving_handle = handle_id[vi];
            get_new_handle_locations();
            doit = true;
        }
    }
    return doit;
}

bool callback_mouse_move(igl::opengl::glfw::Viewer &viewer, int mouse_x, int mouse_y) {
    if (!doit)
        return false;
    if (mouse_mode == SELECT) {
        lasso->strokeAdd(mouse_x, mouse_y);
        return true;
    }
    if ((mouse_mode == TRANSLATE) || (mouse_mode == ROTATE)) {
        if (mouse_mode == TRANSLATE) {
            translation = computeTranslation(viewer,
                                             mouse_x,
                                             down_mouse_x,
                                             mouse_y,
                                             down_mouse_y,
                                             handle_centroids.row(moving_handle));
        } else {
            rotation = computeRotation(viewer,
                                       mouse_x,
                                       down_mouse_x,
                                       mouse_y,
                                       down_mouse_y,
                                       handle_centroids.row(moving_handle));
        }
        get_new_handle_locations();
#ifndef UPDATE_ONLY_ON_UP
        solve(viewer);
        down_mouse_x = mouse_x;
        down_mouse_y = mouse_y;
#endif
        return true;

    }
    return false;
}

bool callback_mouse_up(igl::opengl::glfw::Viewer &viewer, int button, int modifier) {
    if (!doit)
        return false;
    doit = false;
    if (mouse_mode == SELECT) {
        selected_v.resize(0, 1);
        lasso->strokeFinish(selected_v);
        return true;
    }

    if ((mouse_mode == TRANSLATE) || (mouse_mode == ROTATE)) {
#ifdef UPDATE_ONLY_ON_UP
        if(moving_handle>=0)
          solve(viewer);
#endif
        translation.setZero();
        rotation.setZero();
        rotation[3] = 1.;
        moving_handle = -1;

        compute_handle_centroids();

        return true;
    }

    return false;
};

bool callback_pre_draw_point(igl::opengl::glfw::Viewer &viewer) {
    // Initialize vertex colors
    vertex_colors = Eigen::MatrixXd::Constant(V.rows(), 3, .9);
    // first, color constraints
    int num = handle_id.maxCoeff();
    if (num == 0)
        num = 1;
    for (int i = 0; i < V.rows(); ++i)
        if (handle_id[i] != -1) {
            int r = handle_id[i] % MAXNUMREGIONS;
            vertex_colors.row(i) << regionColors[r][0], regionColors[r][1], regionColors[r][2];
        }
    // then, color selection
    for (int i = 0; i < selected_v.size(); ++i)
        vertex_colors.row(selected_v[i]) << 131. / 255, 131. / 255, 131. / 255.;
    viewer.data().set_colors(vertex_colors);
    // draw the stroke of the selection
    for (unsigned int i = 0; i < lasso->strokePoints.size(); ++i) {
        viewer.data().add_points(lasso->strokePoints[i], Eigen::RowVector3d(0.4, 0.4, 0.4));
        if (i > 1)
            viewer.data().add_edges(lasso->strokePoints[i - 1], lasso->strokePoints[i],
                                    Eigen::RowVector3d(0.7, 0.7, .7));
    }

    if (!retri) {
        // clear points and lines
        viewer.data().set_points(Eigen::MatrixXd::Zero(0, 3), Eigen::MatrixXd::Zero(0, 3));
        viewer.data().set_edges(Eigen::MatrixXd::Zero(0, 3), Eigen::MatrixXi::Zero(0, 3), Eigen::MatrixXd::Zero(0, 3));

        // update the vertex position all the time
        viewer.data().V.resize(V.rows(), 3);
        viewer.data().V << V;
        slice(V, b, 1, VC);
        if (!ifreduce & !retri) {
            slice(VC, R1, 1, C1);
            slice(VC, R2, 1, C2);
            viewer.data().add_edges(C1, C2, Eigen::RowVector3d(1, 0, 0));
        }
        // plot the cage
        viewer.data().point_size = 20;
        if (ifreduce) {
            MatrixXd V_temp, V_tem;
            slice(V, b_bat, 1, V_temp);
            V_tem.resize(b_bat.rows(), V.cols());
            for (int i = 0; i <= b_bat.rows() - 2; ++i) {
                V_tem.row(i) = V.row(b_bat(i + 1));
            }
            V_tem.row(b_bat.rows() - 1) = V.row(b_bat(0));
            viewer.data().add_points(V_temp, Eigen::RowVector3d(1, 0, 0));
            viewer.data().add_edges(V_temp, V_tem, Eigen::RowVector3d(1, 0, 0));
        }
        viewer.data().dirty |= igl::opengl::MeshGL::DIRTY_POSITION;
    }
    viewer.data().add_points(handle_vertex_positions, Eigen::RowVector3d(1, 1, 1));
#ifdef UPDATE_ONLY_ON_UP
    //draw only the moving parts with a white line
    if (moving_handle>=0)
    {
      Eigen::MatrixXd edges(3*F.rows(),6);
      int num_edges = 0;
      for (int fi = 0; fi<F.rows(); ++fi)
      {
        int firstPickedVertex = -1;
        for(int vi = 0; vi<3 ; ++vi)
          if (handle_id[F(fi,vi)] == moving_handle)
          {
            firstPickedVertex = vi;
            break;
          }
        if(firstPickedVertex==-1)
          continue;


        Eigen::Matrix3d points;
        for(int vi = 0; vi<3; ++vi)
        {
          int vertex_id = F(fi,vi);
          if (handle_id[vertex_id] == moving_handle)
          {
            int index = -1;
            // if face is already constrained, find index in the constraints
            (handle_vertices.array()-vertex_id).cwiseAbs().minCoeff(&index);
            points.row(vi) = handle_vertex_positions.row(index);
          }
          else
            points.row(vi) =  V.row(vertex_id);

        }
        edges.row(num_edges++) << points.row(0), points.row(1);
        edges.row(num_edges++) << points.row(1), points.row(2);
        edges.row(num_edges++) << points.row(2), points.row(0);
      }
      edges.conservativeResize(num_edges, Eigen::NoChange);
      viewer.data().add_edges(edges.leftCols(3), edges.rightCols(3), Eigen::RowVector3d(0.9,0.9,0.9));

    }
#endif

    return false;
}

bool callback_pre_draw(igl::opengl::glfw::Viewer &viewer) {
    // Initialize vertex colors
    vertex_colors = Eigen::MatrixXd::Constant(V.rows(), 3, .9);

    // first, color constraints
    int num = handle_id.maxCoeff();
    if (num == 0)
        num = 1;
    for (int i = 0; i < V.rows(); ++i)
        if (handle_id[i] != -1) {
            int r = handle_id[i] % MAXNUMREGIONS;
            vertex_colors.row(i) << regionColors[r][0], regionColors[r][1], regionColors[r][2];
        }
    // then, color selection
    for (int i = 0; i < selected_v.size(); ++i)
        vertex_colors.row(selected_v[i]) << 131. / 255, 131. / 255, 131. / 255.;
    viewer.data().set_colors(vertex_colors);


    // clear points and lines
    viewer.data().set_points(Eigen::MatrixXd::Zero(0, 3), Eigen::MatrixXd::Zero(0, 3));
    viewer.data().set_edges(Eigen::MatrixXd::Zero(0, 3), Eigen::MatrixXi::Zero(0, 3), Eigen::MatrixXd::Zero(0, 3));

    // draw the stroke of the selection
    for (unsigned int i = 0; i < lasso->strokePoints.size(); ++i) {
        viewer.data().add_points(lasso->strokePoints[i], Eigen::RowVector3d(0.4, 0.4, 0.4));
        if (i > 1)
            viewer.data().add_edges(lasso->strokePoints[i - 1], lasso->strokePoints[i],
                                    Eigen::RowVector3d(0.7, 0.7, .7));
    }

    // update the vertex position all the time
    viewer.data().V.resize(V.rows(), 3);
    viewer.data().V << V;

    viewer.data().dirty |= igl::opengl::MeshGL::DIRTY_POSITION;

#ifdef UPDATE_ONLY_ON_UP
    //draw only the moving parts with a white line
    if (moving_handle>=0)
    {
      Eigen::MatrixXd edges(3*F.rows(),6);
      int num_edges = 0;
      for (int fi = 0; fi<F.rows(); ++fi)
      {
        int firstPickedVertex = -1;
        for(int vi = 0; vi<3 ; ++vi)
          if (handle_id[F(fi,vi)] == moving_handle)
          {
            firstPickedVertex = vi;
            break;
          }
        if(firstPickedVertex==-1)
          continue;


        Eigen::Matrix3d points;
        for(int vi = 0; vi<3; ++vi)
        {
          int vertex_id = F(fi,vi);
          if (handle_id[vertex_id] == moving_handle)
          {
            int index = -1;
            // if face is already constrained, find index in the constraints
            (handle_vertices.array()-vertex_id).cwiseAbs().minCoeff(&index);
            points.row(vi) = handle_vertex_positions.row(index);
          }
          else
            points.row(vi) =  V.row(vertex_id);

        }
        edges.row(num_edges++) << points.row(0), points.row(1);
        edges.row(num_edges++) << points.row(1), points.row(2);
        edges.row(num_edges++) << points.row(2), points.row(0);
      }
      edges.conservativeResize(num_edges, Eigen::NoChange);
      viewer.data().add_edges(edges.leftCols(3), edges.rightCols(3), Eigen::RowVector3d(0.9,0.9,0.9));

    }
#endif
    return false;
}

bool callback_key_down(igl::opengl::glfw::Viewer &viewer, unsigned char key, int modifiers) {
    bool handled = false;
    if (key == 'S') {
        mouse_mode = SELECT;
        handled = true;
    }

    if ((key == 'T') && (modifiers == IGL_MOD_ALT)) {
        mouse_mode = TRANSLATE;
        handled = true;
    }

    if ((key == 'R') && (modifiers == IGL_MOD_ALT)) {
        mouse_mode = ROTATE;
        handled = true;
    }
    if (key == 'A') {
        applySelection();
        callback_key_down(viewer, '1', 0);
        handled = true;
    }

    return handled;
}

void onNewHandleID() {
    //store handle vertices too
    int numFree = (handle_id.array() == -1).cast<int>().sum();
    int num_handle_vertices = V.rows() - numFree;
    handle_vertices.setZero(num_handle_vertices);
    handle_vertex_positions.setZero(num_handle_vertices, 3);

    int count = 0;
    for (long vi = 0; vi < V.rows(); ++vi)
        if (handle_id[vi] >= 0)
            handle_vertices[count++] = vi;

    compute_handle_centroids();
}

void applySelection() {
    int index = handle_id.maxCoeff() + 1;
    for (int i = 0; i < selected_v.rows(); ++i) {
        const int selected_vertex = selected_v[i];
        if (handle_id[selected_vertex] == -1)
            handle_id[selected_vertex] = index;
    }
    selected_v.resize(0, 1);

    onNewHandleID();
}

void compute_handle_centroids() {
    //compute centroids of handles
    int num_handles = handle_id.maxCoeff() + 1;
    handle_centroids.setZero(num_handles, 3);

    Eigen::VectorXi num;
    num.setZero(num_handles, 1);
    for (long vi = 0; vi < V.rows(); ++vi) {
        int r = handle_id[vi];
        if (r != -1) {
            handle_centroids.row(r) += V.row(vi);
            num[r]++;
        }
    }

    for (long i = 0; i < num_handles; ++i)
        handle_centroids.row(i) = handle_centroids.row(i).array() / num[i];

}

//computes translation for the vertices of the moving handle based on the mouse motion
Eigen::Vector3f computeTranslation(igl::opengl::glfw::Viewer &viewer,
                                   int mouse_x,
                                   int from_x,
                                   int mouse_y,
                                   int from_y,
                                   Eigen::RowVector3d pt3D) {
    Eigen::Matrix4f modelview = viewer.core.view * viewer.core.model;
    //project the given point (typically the handle centroid) to get a screen space depth
    Eigen::Vector3f proj = igl::project(pt3D.transpose().cast<float>().eval(),
                                        modelview,
                                        viewer.core.proj,
                                        viewer.core.viewport);
    float depth = proj[2];

    double x, y;
    Eigen::Vector3f pos1, pos0;

    //unproject from- and to- points
    x = mouse_x;
    y = viewer.core.viewport(3) - mouse_y;
    pos1 = igl::unproject(Eigen::Vector3f(x, y, depth),
                          modelview,
                          viewer.core.proj,
                          viewer.core.viewport);


    x = from_x;
    y = viewer.core.viewport(3) - from_y;
    pos0 = igl::unproject(Eigen::Vector3f(x, y, depth),
                          modelview,
                          viewer.core.proj,
                          viewer.core.viewport);

    //translation is the vector connecting the two
    Eigen::Vector3f translation = pos1 - pos0;
    return translation;

}


//computes translation for the vertices of the moving handle based on the mouse motion
Eigen::Vector4f computeRotation(igl::opengl::glfw::Viewer &viewer,
                                int mouse_x,
                                int from_x,
                                int mouse_y,
                                int from_y,
                                Eigen::RowVector3d pt3D) {

    Eigen::Vector4f rotation;
    rotation.setZero();
    rotation[3] = 1.;

    Eigen::Matrix4f modelview = viewer.core.view * viewer.core.model;

    //initialize a trackball around the handle that is being rotated
    //the trackball has (approximately) width w and height h
    double w = viewer.core.viewport[2] / 8;
    double h = viewer.core.viewport[3] / 8;

    //the mouse motion has to be expressed with respect to its center of mass
    //(i.e. it should approximately fall inside the region of the trackball)

    //project the given point on the handle(centroid)
    Eigen::Vector3f proj = igl::project(pt3D.transpose().cast<float>().eval(),
                                        modelview,
                                        viewer.core.proj,
                                        viewer.core.viewport);
    proj[1] = viewer.core.viewport[3] - proj[1];

    //express the mouse points w.r.t the centroid
    from_x -= proj[0];
    mouse_x -= proj[0];
    from_y -= proj[1];
    mouse_y -= proj[1];

    //shift so that the range is from 0-w and 0-h respectively (similarly to a standard viewport)
    from_x += w / 2;
    mouse_x += w / 2;
    from_y += h / 2;
    mouse_y += h / 2;

    //get rotation from trackball
    Eigen::Vector4f drot = viewer.core.trackball_angle.coeffs();
    Eigen::Vector4f drot_conj;
    igl::quat_conjugate(drot.data(), drot_conj.data());
    igl::trackball(w, h, float(1.), rotation.data(), from_x, from_y, mouse_x, mouse_y, rotation.data());

    //account for the modelview rotation: prerotate by modelview (place model back to the original
    //unrotated frame), postrotate by inverse modelview
    Eigen::Vector4f out;
    igl::quat_mult(rotation.data(), drot.data(), out.data());
    igl::quat_mult(drot_conj.data(), out.data(), rotation.data());
    return rotation;
}



