// VolEsti (volume computation and sampling library)

// Copyright (c) 2012-2020 Vissarion Fisikopoulos
// Copyright (c) 2018-2020 Apostolos Chalkis
// Copyright (c) 2022-2022 Ioannis Iakovidis

// Contributed and/or modified by Ioannis Iakovidis, as part of Google Summer of
// Code 2022 program.

// Licensed under GNU LGPL.3, see LICENCE file

// References
// Yunbum Kook, Yin Tat Lee, Ruoqi Shen, Santosh S. Vempala. "Sampling with
// Riemannian Hamiltonian
// Monte Carlo in a Constrained Space"

// The log barrier for the domain {lu <= x <= ub}:
//	phi(x) = - sum log(x - lb) - sum log(ub - x).
#ifndef WEIGHTED_TWO_SIDED_BARIER_H
#define WEIGHTED_TWO_SIDED_BARIER_H

#include "Eigen/Eigen"
#include "cartesian_geom/cartesian_kernel.h"
#include "preprocess/crhmc/crhmc_utils.h"
#include <vector>

/// @brief A weighted two sided barrier used by crhmc sampler
/// @tparam Point Point Type
template <class Point> class weighted_two_sided_barrier {

  using NT = typename Point::FT;
  using MT = Eigen::Matrix<NT, Eigen::Dynamic, Eigen::Dynamic>;
  using VT = Eigen::Matrix<NT, Eigen::Dynamic, 1>;

public:
  VT lb;
  VT ub;
  int vdim;
  int n;
  std::vector<int> upperIdx;
  std::vector<int> lowerIdx;
  std::vector<int> freeIdx;
  VT center;
  const NT max_step = 1e16; // largest step size
  const NT regularization_constant = 1e-20; // small regularization to not have a large inverse
  const NT unbounded_center_coord = 1e6;
  MT extraHessian;
  const NT inf = std::numeric_limits<NT>::infinity();

  VT w;

  weighted_two_sided_barrier(VT const &_lb, VT const &_ub, VT const &_w,
                          int _vdim = 1) {
    set_bound(_lb, _ub);
    w = _w;
    vdim = _vdim;
    extraHessian = regularization_constant * VT::Ones(n,1);
  }
  weighted_two_sided_barrier() { vdim = 1; }

  VT gradient(VT const &x) {
    return w.cwiseQuotient(ub - x) - w.cwiseQuotient(x - lb);
  }

  VT hessian(VT const &x) {
    VT d = w.cwiseQuotient((ub - x).cwiseProduct((ub - x))) +
           w.cwiseQuotient((x - lb).cwiseProduct((x - lb)));
    return d + extraHessian;
  }
  MT hessian(MT const &x){
    MT d = (((- x).colwise()+ub).cwiseProduct(((- x).colwise()+ub))).cwiseInverse() +
           ((x.colwise() - lb).cwiseProduct((x.colwise() - lb))).cwiseInverse();
    return w.asDiagonal()*d + extraHessian;
  }
  VT tensor(VT const &x) {
    VT d = 2 * w.cwiseQuotient(((ub - x).cwiseProduct((ub - x))).cwiseProduct((ub - x))) -
           2 * w.cwiseQuotient(((x - lb).cwiseProduct((x - lb))).cwiseProduct((x - lb)));
    return d;
  }
  MT tensor(MT const &x) {
    MT d = 2 * ((((-x).colwise()+ub).cwiseProduct(((-x).colwise()+ub))).cwiseProduct(((-x).colwise()+ub))).cwiseInverse() -
           2 * (((x.colwise() - lb).cwiseProduct(( x.colwise() - lb))).cwiseProduct(( x.colwise() - lb))).cwiseInverse();
    return w.asDiagonal()*d;
  }
  MT quadratic_form_gradient(MT const &x, MT const &u) {
    // Output the -grad of u' (hess phi(x)) u.
    return (u.cwiseProduct(u)).cwiseProduct(tensor(x));
  }
  VT quadratic_form_gradient(VT const &x, VT const &u) {
    // Output the -grad of u' (hess phi(x)) u.

    return (u.cwiseProduct(u)).cwiseProduct(tensor(x));
  }
  NT step_size(VT const &x, VT const &v) {
    // Output the maximum step size from x with direction v or -v.

    // check positive direction
    VT temp = (v.array() > 0).select((ub - x).cwiseQuotient(v), max_step);
    NT t1 = temp.minCoeff();

    // check negative direction
    temp = (v.array() < 0).select((lb - x).cwiseQuotient(v), max_step);
    NT t2 = temp.minCoeff();

    return std::min(t1, t2);
  }
  VT boundary_distance(VT const &x) {
    // Output the distance of x with its closest boundary for each
    // coordinate
    return ((x - lb).cwiseMin(ub - x)).cwiseAbs();
  }

  bool feasible(VT const &x) {
    return (x.array() > lb.array() && x.array() < ub.array()).all();
  }
  VT feasible(MT const &x) {
    VT result=VT::Ones(x.cols());
    for(int i=0;i<x.cols();i++){
      result(i)=(x.col(i).array() > lb.array() && x.col(i).array() < ub.array()).all();
    }
    return result;
  }
  void set_bound(VT const &_lb, VT const &_ub) {

    lb = _lb;
    ub = _ub;
    n = lb.rows();
    extraHessian = regularization_constant * VT::Ones(n);
    int x1 = 0, x2 = 0, x3 = 0;
    for (int i = 0; i < n; i++) {
      if (lb(i) == -inf) {
        upperIdx.push_back(i);
        x1++;
      }
      if (ub(i) == inf) {
        lowerIdx.push_back(i);
        x2++;
      }
      if (ub(i) == inf && lb(i) == -inf) {
        freeIdx.push_back(i);
      }
    }

    VT c = (ub + lb) / 2;
    VT bias1=VT::Ones(x2, 1) * unbounded_center_coord;
    volesti_saxpy(c,lb,bias1,lowerIdx,lowerIdx);
    VT bias2=-VT::Ones(x1, 1) * unbounded_center_coord;
    volesti_saxpy(c,ub,bias2,upperIdx,upperIdx);
    set(c, freeIdx, 0.0);

    center = c;
  }

  std::pair<VT, VT> boundary(VT const &x) {
    // Output the normal at the boundary around x for each barrier.
    // Assume: only 1 vector is given
    VT A = VT::Ones(x.rows(), 1);
    VT b = ub;

    b = (x.array() < center.array()).select(-lb, b);
    A = (x.array() < center.array()).select(-A, A);

    return std::make_pair(A, b);
  }
};
#endif
