// specialization of finite_element for L2 on hexahedron geometry
//
// this specialization defines shape functions (and their gradients) that
// interpolate at Gauss-Lobatto nodes for the appropriate polynomial order
// 
// note: mfem assumes the parent element domain is [0,1]x[0,1]x[0,1]
template <int p, int c>
struct finite_element<Geometry::Hexahedron, L2<p, c> > {
  static constexpr auto geometry   = Geometry::Hexahedron;
  static constexpr auto family     = Family::H1;
  static constexpr int  components = c;
  static constexpr int  dim        = 3;
  static constexpr int  ndof       = (p + 1) * (p + 1) * (p + 1);

  using residual_type = typename std::conditional< components == 1, 
    tensor< double, ndof >,
    tensor< double, ndof, components >
  >::type;

  static constexpr tensor<double, ndof> shape_functions(tensor<double, dim> xi)
  {
    auto N_xi   = GaussLegendreInterpolation<p + 1>(xi[0]);
    auto N_eta  = GaussLegendreInterpolation<p + 1>(xi[1]);
    auto N_zeta = GaussLegendreInterpolation<p + 1>(xi[2]);

    int count = 0;

    tensor<double, ndof> N{};
    for (int k = 0; k < p + 1; k++) {
      for (int j = 0; j < p + 1; j++) {
        for (int i = 0; i < p + 1; i++) {
          N[count++] = N_xi[i] * N_eta[j] * N_zeta[k];
        }
      }
    }
    return N;
  }

  static constexpr tensor<double, ndof, dim> shape_function_gradients(tensor<double, dim> xi)
  {
    auto N_xi    = GaussLegendreInterpolation<p + 1>(xi[0]);
    auto N_eta   = GaussLegendreInterpolation<p + 1>(xi[1]);
    auto N_zeta  = GaussLegendreInterpolation<p + 1>(xi[2]);
    auto dN_xi   = GaussLegendreInterpolationDerivative<p + 1>(xi[0]);
    auto dN_eta  = GaussLegendreInterpolationDerivative<p + 1>(xi[1]);
    auto dN_zeta = GaussLegendreInterpolationDerivative<p + 1>(xi[2]);

    int count = 0;

    // clang-format off
    tensor<double, ndof, dim> dN{};
    for (int k = 0; k < p + 1; k++) {
      for (int j = 0; j < p + 1; j++) {
        for (int i = 0; i < p + 1; i++) {
          dN[count++] = {
            dN_xi[i] *  N_eta[j] *  N_zeta[k], 
             N_xi[i] * dN_eta[j] *  N_zeta[k],
             N_xi[i] *  N_eta[j] * dN_zeta[k]
          };
        }
      }
    }
    return dN;
    // clang-format on
  }

};
