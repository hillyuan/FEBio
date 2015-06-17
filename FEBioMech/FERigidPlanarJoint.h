//
//  FERigidPlanarJoint.h
//  FEBioMech
//
//  Created by Gerard Ateshian on 5/28/15.
//  Copyright (c) 2015 febio.org. All rights reserved.
//

#ifndef __FEBioMech__FERigidPlanarJoint__
#define __FEBioMech__FERigidPlanarJoint__

#include "FERigidConnector.h"

//-----------------------------------------------------------------------------
//! The FERigidPlanarJoint class implements a planar joint. The rigid joint
//! allows the user to connect two rigid bodies at a point in space
//! and allow 2D translation in a plane and rotation about the plane normal.

class FERigidPlanarJoint : public FERigidConnector
{
public:
    //! constructor
    FERigidPlanarJoint(FEModel* pfem);
    
    //! destructor
    virtual ~FERigidPlanarJoint() {}
    
    //! initialization
    bool Init();
    
    //! calculates the joint forces
    void Residual(FEGlobalVector& R, const FETimePoint& tp);
    
    //! calculates the joint stiffness
    void StiffnessMatrix(FESolver* psolver, const FETimePoint& tp);
    
    //! calculate Lagrangian augmentation
    bool Augment(int naug, const FETimePoint& tp);
    
    //! serialize data to archive
    void Serialize(DumpFile& ar);
    
    //! create a shallow copy
    void ShallowCopy(DumpStream& dmp, bool bsave);
    
    //! update state
    void Update(const FETimePoint& tp);
    
    //! Reset data
    void Reset();
    
public:
    vec3d	m_q0;	//! initial position of joint
    vec3d	m_qa0;	//! initial relative position vector of joint w.r.t. A
    vec3d	m_qb0;	//! initial relative position vector of joint w.r.t. B
    
    vec3d	m_e0[3];	//! initial joint basis
    vec3d	m_ea0[3];	//! initial joint basis w.r.t. A
    vec3d	m_eb0[3];	//! initial joint basis w.r.t. B
    
    vec3d	m_L;	//! Lagrange multiplier for constraining force
    double	m_eps;	//! penalty factor for constraining force
    
    vec3d	m_U;	//! Lagrange multiplier for constraining moment
    double	m_ups;	//! penalty factor for constraining moment
    
    double	m_atol;	//! augmented Lagrangian tolerance
    double  m_gtol; //! augmented Lagrangian gap tolerance
    double  m_qtol; //! augmented Lagrangian angular gap tolerance
    int     m_naugmin;  //! minimum number of augmentations
    int     m_naugmax;  //! maximum number of augmentations
    
    double  m_qpx;   //! prescribed rotation along first axis
    double  m_dpy;   //! prescribed translation along second axis
    double  m_dpz;   //! prescribed translation along third axis
    bool    m_bqx;   //! flag for prescribing rotation along first axis
    bool    m_bdy;   //! flag for prescribing translation along second axis
    bool    m_bdz;   //! flag for prescribing translation along third axis
    
protected:
    bool	m_binit;
    
    DECLARE_PARAMETER_LIST();
};

#endif /* defined(__FEBioMech__FERigidPlanarJoint__) */
