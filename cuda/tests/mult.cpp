// Copyright (c) 2017, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-734707. All Rights
// reserved. See files LICENSE and NOTICE for details.
//
// This file is part of CEED, a collection of benchmarks, miniapps, software
// libraries and APIs for efficient high-order finite element and spectral
// element discretizations for exascale applications. For more information and
// source code availability see http://github.com/ceed.
//
// The CEED research is supported by the Exascale Computing Project 17-SC-20-SC,
// a collaborative effort of two U.S. Department of Energy organizations (Office
// of Science and the National Nuclear Security Administration) responsible for
// the planning and preparation of a capable exascale ecosystem, including
// software, applications, hardware, advanced system engineering and early
// testbed platforms, in support of the nation's exascale computing imperative.
#include "../cuda.hpp"
#include "../../laghos_solver.hpp"
#include <sys/time.h>
#include <unistd.h>

namespace mfem {

  // ***************************************************************************
  // CudaPm1APOperator
  // ***************************************************************************
  class CudaPm1APOperator : public CudaOperator{
  private:
    const CudaOperator &Rt;
    const CudaOperator &A;
    const CudaOperator &P;
    mutable CudaVector Px;
    mutable CudaVector APx;
  public:
    /// Construct the RAP operator given R^T, A and P.
    CudaPm1APOperator(const CudaOperator &Rt_, const CudaOperator &A_, const CudaOperator &P_)
      : CudaOperator(Rt_.Width(), P_.Width()), Rt(Rt_), A(A_), P(P_),
        Px(P.Height()), APx(A.Height()) { }
    /// Operator application.
    void Mult(const CudaVector & x, CudaVector & y) const {
      push(Pm1AP,SkyBlue);
      P.Mult(x, Px);
      A.Mult(Px, APx);
      Rt.MultTranspose(APx, y);
      pop();
    }
    /// Application of the transpose.
    void MultTranspose(const CudaVector & x, CudaVector & y) const {
      push(Pm1APT,SkyBlue);
      Rt.Mult(x, APx);
      A.MultTranspose(APx, Px);
      P.MultTranspose(Px, y);
      pop();
    }
  };

  // ***************************************************************************
  // CudaIdOperator
  // ***************************************************************************
  class CudaIdOperator: public CudaOperator {
  public:
    CudaIdOperator(int s = 0) { height = width = s; }
    void Mult(const CudaVector &x, CudaVector &y) const  {
      push(Id=,DarkCyan);
      //usleep(1);
      y=x;
      pop();
    }
    void MultTranspose(const CudaVector &x, CudaVector &y) const {
      push(Id=T,DarkCyan);
      //usleep(1);
      y=x;
      pop();
    }
  };

  // ***************************************************************************
  __global__ void iniK(void){}

  // ***************************************************************************
  bool multTest(ParMesh *pmesh, const int order, const int max_step){
    struct timeval st, et;
    const int nb_step = (max_step>0)?max_step:100;
    assert(order>=1);
    
    const int dim = pmesh->Dimension();
    const H1_FECollection fec(order, dim);
    CudaFiniteElementSpace fes(pmesh, &fec, 1);
    
    const int lsize = fes.GetVSize();
    const int ltsize = fes.GetTrueVSize();
    const HYPRE_Int gsize = fes.GlobalTrueVSize();
    
    if (rconfig::Get().Root())
      mfem::out << "Number of global dofs: " << gsize << std::endl;
    if (rconfig::Get().Root())
      mfem::out << "Number of local dofs: " << lsize << std::endl;
    
    const CudaOperator &prolong = *fes.GetProlongationOperator();
    const CudaOperator &testP  = prolong;
    const CudaOperator &trialP = prolong;
    const CudaIdOperator Id(lsize);
    
    CudaPm1APOperator Pm1AP(testP,Id,trialP);
    CudaVector x(ltsize); x=1.0;
    CudaVector y(lsize);
    
    MPI_Barrier(pmesh->GetComm());
    cudaDeviceSynchronize();

    // Do one RAP Mult/MultTranspose to set MPI's buffers
    Pm1AP.Mult(x,y);
    Pm1AP.MultTranspose(x,y);
    
    MPI_Barrier(pmesh->GetComm());
    cudaDeviceSynchronize();
    // Now let go the markers
    rconfig::Get().Nvvp(true);
    cuProfilerStart();
    
    // Launch first dummy kernel for profiling overhead to start
    push(iniK,Green);
    iniK<<<128,1>>>();
    pop();
    
    MPI_Barrier(pmesh->GetComm());
    cudaDeviceSynchronize();

    gettimeofday(&st, NULL);
    for(int i=0;i<nb_step;i++){
      cudaDeviceSynchronize(); // used with nvvp
      push(SkyBlue);
      Pm1AP.Mult(x, y);
      pop();
      push(SkyBlue);
      Pm1AP.MultTranspose(x, y);
      pop();
    }
    // We MUST sync after to make sure every kernel has completed
    // or play with the -sync flag to enforce it with the push/pop
    cudaDeviceSynchronize();
    gettimeofday(&et, NULL);
    const float alltime = ((et.tv_sec-st.tv_sec)*1.0e3+(et.tv_usec - st.tv_usec)/1.0e3);
    if (rconfig::Get().Root())
      printf("\033[32m[laghos] Elapsed time = %f ms/step\33[m\n", alltime/nb_step);
    return true;
  }

} // namespace mfem
