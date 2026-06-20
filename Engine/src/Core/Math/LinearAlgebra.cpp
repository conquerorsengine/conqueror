#include "LinearAlgebra.h"
#include <cmath>
#include <stdexcept>

namespace Conqueror::Math {
    LinearAlgebra::Matrix LinearAlgebra::CreateMatrix(int rows, int cols, double val) {
        return Matrix(rows, Vector(cols, val));
    }

    LinearAlgebra::Matrix LinearAlgebra::Identity(int n) {
        Matrix I = CreateMatrix(n, n, 0.0);
        for (int i = 0; i < n; ++i) I[i][i] = 1.0;
        return I;
    }

    LinearAlgebra::Matrix LinearAlgebra::Multiply(const Matrix& A, const Matrix& B) {
        if (A.empty() || B.empty() || A[0].size() != B.size()) return {};
        int rows = A.size(), cols = B[0].size(), inner = B.size();
        Matrix C = CreateMatrix(rows, cols, 0.0);
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                for (int k = 0; k < inner; ++k) {
                    C[i][j] += A[i][k] * B[k][j];
                }
            }
        }
        return C;
    }

    LinearAlgebra::Vector LinearAlgebra::Multiply(const Matrix& A, const Vector& x) {
        if (A.empty() || A[0].size() != x.size()) return {};
        Vector y(A.size(), 0.0);
        for (size_t i = 0; i < A.size(); ++i) {
            for (size_t j = 0; j < x.size(); ++j) {
                y[i] += A[i][j] * x[j];
            }
        }
        return y;
    }

    LinearAlgebra::Matrix LinearAlgebra::Transpose(const Matrix& A) {
        if (A.empty()) return {};
        Matrix T = CreateMatrix(A[0].size(), A.size(), 0.0);
        for (size_t i = 0; i < A.size(); ++i) {
            for (size_t j = 0; j < A[0].size(); ++j) {
                T[j][i] = A[i][j];
            }
        }
        return T;
    }

    bool LinearAlgebra::LUDecomposition(const Matrix& A, Matrix& L, Matrix& U) {
        int n = A.size();
        if (n == 0 || A[0].size() != (size_t)n) return false;
        L = CreateMatrix(n, n, 0.0);
        U = CreateMatrix(n, n, 0.0);
        
        for (int i = 0; i < n; ++i) {
            for (int k = i; k < n; ++k) {
                double sum = 0;
                for (int j = 0; j < i; ++j) sum += L[i][j] * U[j][k];
                U[i][k] = A[i][k] - sum;
            }
            for (int k = i; k < n; ++k) {
                if (i == k) L[i][i] = 1.0;
                else {
                    double sum = 0;
                    for (int j = 0; j < i; ++j) sum += L[k][j] * U[j][i];
                    if (std::abs(U[i][i]) < 1e-12) return false;
                    L[k][i] = (A[k][i] - sum) / U[i][i];
                }
            }
        }
        return true;
    }

    LinearAlgebra::Vector LinearAlgebra::SolveLU(const Matrix& L, const Matrix& U, const Vector& b) {
        int n = L.size();
        Vector y(n, 0.0), x(n, 0.0);
        for (int i = 0; i < n; ++i) {
            double sum = 0;
            for (int j = 0; j < i; ++j) sum += L[i][j] * y[j];
            y[i] = b[i] - sum;
        }
        for (int i = n - 1; i >= 0; --i) {
            double sum = 0;
            for (int j = i + 1; j < n; ++j) sum += U[i][j] * x[j];
            x[i] = (y[i] - sum) / U[i][i];
        }
        return x;
    }

    LinearAlgebra::Vector LinearAlgebra::SolveGaussian(Matrix A, Vector b) {
        int n = A.size();
        for (int i = 0; i < n; ++i) {
            int maxRow = i;
            for (int k = i + 1; k < n; ++k) {
                if (std::abs(A[k][i]) > std::abs(A[maxRow][i])) maxRow = k;
            }
            std::swap(A[i], A[maxRow]);
            std::swap(b[i], b[maxRow]);
            
            if (std::abs(A[i][i]) < 1e-12) return {};
            
            for (int k = i + 1; k < n; ++k) {
                double factor = A[k][i] / A[i][i];
                b[k] -= factor * b[i];
                for (int j = i; j < n; ++j) A[k][j] -= factor * A[i][j];
            }
        }
        
        Vector x(n, 0.0);
        for (int i = n - 1; i >= 0; --i) {
            double sum = 0;
            for (int j = i + 1; j < n; ++j) sum += A[i][j] * x[j];
            x[i] = (b[i] - sum) / A[i][i];
        }
        return x;
    }

    bool LinearAlgebra::CholeskyDecomposition(const Matrix& A, Matrix& L) {
        int n = A.size();
        L = CreateMatrix(n, n, 0.0);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j <= i; ++j) {
                double sum = 0;
                for (int k = 0; k < j; ++k) sum += L[i][k] * L[j][k];
                if (i == j) {
                    double val = A[i][i] - sum;
                    if (val <= 0) return false;
                    L[i][j] = std::sqrt(val);
                } else {
                    L[i][j] = (A[i][j] - sum) / L[j][j];
                }
            }
        }
        return true;
    }
    
    double LinearAlgebra::Determinant(const Matrix& A) {
        int n = A.size();
        if (n == 0) return 0;
        if (n == 1) return A[0][0];
        if (n == 2) return A[0][0]*A[1][1] - A[0][1]*A[1][0];
        
        Matrix L, U;
        if (LUDecomposition(A, L, U)) {
            double det = 1.0;
            for (int i = 0; i < n; ++i) det *= U[i][i];
            return det;
        }
        return 0.0;
    }

    LinearAlgebra::Matrix LinearAlgebra::Inverse(const Matrix& A) {
        int n = A.size();
        Matrix inv = CreateMatrix(n, n, 0.0);
        Matrix L, U;
        if (!LUDecomposition(A, L, U)) return {};
        for (int i = 0; i < n; ++i) {
            Vector b(n, 0.0);
            b[i] = 1.0;
            Vector col = SolveLU(L, U, b);
            for (int j = 0; j < n; ++j) inv[j][i] = col[j];
        }
        return inv;
    }
}
