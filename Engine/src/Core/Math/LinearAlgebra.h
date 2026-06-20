#pragma once
#include "Core/Base/Base.h"
#include <vector>

namespace Conqueror::Math {
    class CQ_API LinearAlgebra {
    public:
        using Matrix = std::vector<std::vector<double>>;
        using Vector = std::vector<double>;

        static Matrix CreateMatrix(int rows, int cols, double val = 0.0);
        static Matrix Identity(int n);
        static Matrix Multiply(const Matrix& A, const Matrix& B);
        static Vector Multiply(const Matrix& A, const Vector& x);
        static Matrix Transpose(const Matrix& A);

        static bool LUDecomposition(const Matrix& A, Matrix& L, Matrix& U);
        static Vector SolveLU(const Matrix& L, const Matrix& U, const Vector& b);
        
        static Vector SolveGaussian(Matrix A, Vector b);
        
        static bool CholeskyDecomposition(const Matrix& A, Matrix& L);
        
        static double Determinant(const Matrix& A);
        static Matrix Inverse(const Matrix& A);
    };
}
