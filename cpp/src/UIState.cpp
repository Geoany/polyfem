#include "UIState.hpp"

#include <igl/colormap.h>
#include <igl/triangle/triangulate.h>
#include <igl/copyleft/tetgen/tetrahedralize.h>
#include <igl/Timer.h>


#include <nanogui/formhelper.h>
#include <nanogui/screen.h>


// ... or using a custom callback
  //       viewer_.ngui->addVariable<bool>("bool",[&](bool val) {
  //     boolVariable = val; // set
  // },[&]() {
  //     return boolVariable; // get
  // });


using namespace Eigen;


namespace poly_fem
{
	UIState::UIState()
	: state(State::state())
	{ }

	void UIState::plot_function(const MatrixXd &fun, double min, double max)
	{
		MatrixXd col;

		if(state.linear_elasticity)
		{
			const MatrixXd ffun = (fun.array()*fun.array()).colwise().sum().sqrt(); //norm of displacement, maybe replace with stress

			if(min < max)
				igl::colormap(igl::COLOR_MAP_TYPE_INFERNO, fun, min, max, col);
			else
				igl::colormap(igl::COLOR_MAP_TYPE_INFERNO, fun, true, col);

			MatrixXd tmp = vis_pts;

			for(long i = 0; i < fun.cols(); ++i) //apply displacement
				tmp.col(i) += fun.col(i);

			viewer.data.set_mesh(tmp, vis_faces);
		}
		else
		{

			if(min < max)
				igl::colormap(igl::COLOR_MAP_TYPE_INFERNO, fun, min, max, col);
			else
				igl::colormap(igl::COLOR_MAP_TYPE_INFERNO, fun, true, col);

			if(state.mesh.is_volume())
				viewer.data.set_mesh(vis_pts, vis_faces);
			else
			{
				MatrixXd tmp;
				tmp.resize(fun.rows(),3);
				tmp.col(0)=vis_pts.col(0);
				tmp.col(1)=vis_pts.col(1);
				tmp.col(2)=fun;
				viewer.data.set_mesh(tmp, vis_faces);
			}
		}

		viewer.data.set_colors(col);
	}


	UIState &UIState::ui_state(){
		static UIState instance;

		return instance;
	}

	void UIState::init(const std::string &mesh_path, const int n_refs, const int problem_num)
	{
		state.init(mesh_path, n_refs, problem_num);

		auto clear_func = [&](){viewer.data.clear(); };

		auto show_mesh_func = [&](){
			clear_func();
			viewer.data.set_mesh(tri_pts, tri_faces);
		};

		auto show_vis_mesh_func = [&](){
			clear_func();
			viewer.data.set_mesh(vis_pts, vis_faces);
		};

		auto show_nodes_func = [&](){
			// for(std::size_t i = 0; i < bounday_nodes.size(); ++i)
			// 	std::cout<<bounday_nodes[i]<<std::endl;

			for(std::size_t i = 0; i < state.bases.size(); ++i)
			{
				const std::vector<Basis> &basis = state.bases[i];
				for(std::size_t j = 0; j < basis.size(); ++j)
				{
					MatrixXd nn = MatrixXd::Zero(basis[j].node().rows(), 3);
					nn.block(0, 0, nn.rows(), basis[j].node().cols()) = basis[j].node();

					VectorXd txt_p = nn.row(0);
					for(long k = 0; k < txt_p.size(); ++k)
						txt_p(k) += 0.02;

					MatrixXd col = MatrixXd::Zero(basis[j].node().rows(), 3);
					if(std::find(state.bounday_nodes.begin(), state.bounday_nodes.end(), basis[j].global_index()) != state.bounday_nodes.end())
						col.col(0).setOnes();


					viewer.data.add_points(nn, col);
					viewer.data.add_label(txt_p, std::to_string(basis[j].global_index()));
				}
			}
		};

		auto show_quadrature_func = [&](){
			for(std::size_t i = 0; i < state.values.size(); ++i)
			{
				const ElementAssemblyValues &vals = state.values[i];
				viewer.data.add_points(vals.val, MatrixXd::Zero(vals.val.rows(), 3));

				for(long j = 0; j < vals.val.rows(); ++j)
					viewer.data.add_label(vals.val.row(j), std::to_string(j));
			}
		};

		auto show_rhs_func = [&](){
			MatrixXd global_rhs;
			state.interpolate_function(state.rhs, local_vis_pts, global_rhs);

			plot_function(global_rhs, 0, 1);
		};


		auto show_sol_func = [&](){
			MatrixXd global_sol;
			state.interpolate_function(state.sol, local_vis_pts, global_sol);
			plot_function(global_sol, 0, 1);
		};


		auto show_error_func = [&](){

			MatrixXd global_sol;
			state.interpolate_function(state.sol, local_vis_pts, global_sol);

			MatrixXd exact_sol;
			state.problem.exact(vis_pts, exact_sol);

			const MatrixXd err = (global_sol - exact_sol).array().abs();
			plot_function(err);
		};


		auto show_basis_func = [&](){
			if(vis_basis < 0 || vis_basis >= state.n_bases) return;

			MatrixXd fun = MatrixXd::Zero(state.n_bases, 1);
			fun(vis_basis) = 1;

			MatrixXd global_fun;
			state.interpolate_function(fun, local_vis_pts, global_fun);
			// global_fun /= 100;
			plot_function(global_fun, 0, 1.);
		};


		auto build_vis_mesh_func = [&](){
			igl::Timer timer; timer.start();
			std::cout<<"Building vis mesh..."<<std::flush;

			if(state.mesh.is_volume())
			{
				MatrixXd pts(8,3); pts <<
				0, 0, 0,
				0, 1, 0,
				1, 1, 0,
				1, 0, 0,

				0, 0, 1, //4
				0, 1, 1,
				1, 1, 1,
				1, 0, 1;

				Eigen::MatrixXi faces(12,3); faces <<
				1, 2, 0,
				0, 2, 3,

				5, 4, 6,
				4, 7, 6,

				1, 0, 4,
				1, 4, 5,

				2, 1, 5,
				2, 5, 6,

				3, 2, 6,
				3, 6, 7,

				0, 3, 7,
				0, 7, 4;

				clear_func();

				MatrixXi tets;
				igl::copyleft::tetgen::tetrahedralize(pts, faces, "Qpq1.414a0.001", local_vis_pts, tets, local_vis_faces);
			}
			else
			{
				MatrixXd pts(4,2); pts <<
				0,0,
				0,1,
				1,1,
				1,0;

				MatrixXi E(4,2); E <<
				0,1,
				1,2,
				2,3,
				3,0;

				MatrixXd H(0,2);
				std::stringstream buf;
				buf.precision(100);
				buf.setf(std::ios::fixed, std::ios::floatfield);
				buf<<"Qqa"<<(0.0001*state.mesh.n_elements());
				igl::triangle::triangulate(pts, E, H, buf.str(), local_vis_pts, local_vis_faces);
			}

			vis_pts.resize(local_vis_pts.rows()*state.mesh.n_elements(), local_vis_pts.cols());
			vis_faces.resize(local_vis_faces.rows()*state.mesh.n_elements(), 3);

			MatrixXd mapped, tmp;
			for(std::size_t i = 0; i < state.bases.size(); ++i)
			{
				const std::vector<Basis> &bs = state.bases[i];
				Basis::eval_geom_mapping(local_vis_pts, bs, mapped);
				vis_pts.block(i*local_vis_pts.rows(), 0, local_vis_pts.rows(), mapped.cols()) = mapped;
				vis_faces.block(i*local_vis_faces.rows(), 0, local_vis_faces.rows(), 3) = local_vis_faces.array() + int(i)*int(local_vis_pts.rows());
			}

			timer.stop();
			std::cout<<" took "<<timer.getElapsedTime()<<"s"<<std::endl;

			if(skip_visualization) return;

			clear_func();
			show_vis_mesh_func();
		};


		auto load_mesh_func = [&](){
			state.load_mesh();
			state.mesh.triangulate_faces(tri_faces, tri_pts);

			if(skip_visualization) return;

			clear_func();
			show_mesh_func();
		};

		auto build_basis_func = [&](){
			state.build_basis();

			if(skip_visualization) return;
			clear_func();
			show_mesh_func();
			show_nodes_func();
		};


		auto compute_assembly_vals_func = [&]() {
			state.compute_assembly_vals();

			if(skip_visualization) return;
			clear_func();
			show_mesh_func();
			show_quadrature_func();
		};

		auto assemble_stiffness_mat_func = [&]() {
			state.assemble_stiffness_mat();
		};


		auto assemble_rhs_func = [&]() {
			state.assemble_rhs();

			if(skip_visualization) return;
			// clear_func();
			// show_rhs_func();
		};

		auto solve_problem_func = [&]() {
			state.solve_problem();

			if(skip_visualization) return;
			clear_func();
			show_sol_func();
		};

		auto compute_errors_func = [&]() {
			state.compute_errors();

			if(skip_visualization) return;
			clear_func();
			show_error_func();
		};




		viewer.callback_init = [&](igl::viewer::Viewer& viewer_)
		{
			viewer_.ngui->addWindow(Eigen::Vector2i(220,10),"PolyFEM");

			viewer_.ngui->addGroup("Settings");

			viewer_.ngui->addVariable("quad order", state.quadrature_order);
			viewer_.ngui->addVariable("b samples", state.n_boundary_samples);

			viewer_.ngui->addVariable("mesh path", state.mesh_path);
			viewer_.ngui->addVariable("n refs", state.n_refs);

			viewer_.ngui->addVariable("spline basis", state.use_splines);

			viewer_.ngui->addVariable("elasticity", state.linear_elasticity);

			viewer_.ngui->addVariable<ProblemType>("Problem",
				[&](ProblemType val) { state.problem.set_problem_num(val); },
				[&]() { return ProblemType(state.problem.problem_num()); }
				)->setItems({"Linear","Quadratic","Franke", "Elastic"});

			viewer_.ngui->addVariable("skip visualization", skip_visualization);

			viewer_.ngui->addGroup("Runners");
			viewer_.ngui->addButton("Load mesh", load_mesh_func);
			viewer_.ngui->addButton("Build  basis", build_basis_func);
			viewer_.ngui->addButton("Compute vals", compute_assembly_vals_func);
			viewer_.ngui->addButton("Build vis mesh", build_vis_mesh_func);

			viewer_.ngui->addButton("Assemble stiffness", assemble_stiffness_mat_func);
			viewer_.ngui->addButton("Assemble rhs", assemble_rhs_func);
			viewer_.ngui->addButton("Solve", solve_problem_func);
			viewer_.ngui->addButton("Compute errors", compute_errors_func);

			viewer_.ngui->addButton("Run all", [&](){
				load_mesh_func();
				build_basis_func();

				if(!skip_visualization)
					build_vis_mesh_func();

				compute_assembly_vals_func();
				assemble_stiffness_mat_func();
				assemble_rhs_func();
				solve_problem_func();
				compute_errors_func();
			});

			viewer_.ngui->addWindow(Eigen::Vector2i(400,10),"Debug");
			viewer_.ngui->addButton("Clear", clear_func);
			viewer_.ngui->addButton("Show mesh", show_mesh_func);
			viewer_.ngui->addButton("Show vis mesh", show_vis_mesh_func);
			viewer_.ngui->addButton("Show nodes", show_nodes_func);
			viewer_.ngui->addButton("Show quadrature", show_quadrature_func);
			viewer_.ngui->addButton("Show rhs", show_rhs_func);
			viewer_.ngui->addButton("Show sol", show_sol_func);
			viewer_.ngui->addButton("Show error", show_error_func);

			viewer_.ngui->addVariable("basis num",vis_basis);
			viewer_.ngui->addButton("Show basis", show_basis_func);

			// viewer_.ngui->addGroup("Stats");
			// viewer_.ngui->addVariable("NNZ", Type &value)


			viewer_.screen->performLayout();

			return false;
		};

		viewer.launch();
	}

	void UIState::sertialize(const std::string &name)
	{

	}

}