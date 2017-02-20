//
//  FEMultiphasicShellDomain.cpp
//  FEBioMix
//
//  Created by Gerard Ateshian on 2/12/17.
//  Copyright © 2017 febio.org. All rights reserved.
//

#include "FEMultiphasicShellDomain.h"
#include "FEMultiphasicMultigeneration.h"
#include "FECore/FEModel.h"
#include "FECore/FEAnalysis.h"
#include "FECore/log.h"
#include "FECore/DOFS.h"

#ifndef SQR
#define SQR(x) ((x)*(x))
#endif

//-----------------------------------------------------------------------------
FEMultiphasicShellDomain::FEMultiphasicShellDomain(FEModel* pfem) : FESSIShellDomain(pfem), FEMultiphasicDomain(pfem)
{
    m_dofU = pfem->GetDOFIndex("u");
    m_dofV = pfem->GetDOFIndex("v");
    m_dofW = pfem->GetDOFIndex("w");
}

//-----------------------------------------------------------------------------
void FEMultiphasicShellDomain::SetMaterial(FEMaterial* pmat)
{
    m_pMat = dynamic_cast<FEMultiphasic*>(pmat);
    assert(m_pMat);
}

//-----------------------------------------------------------------------------
//! Unpack the element LM data.
void FEMultiphasicShellDomain::UnpackLM(FEElement& el, vector<int>& lm)
{
    // get nodal DOFS
    const int nsol = m_pMat->Solutes();
    
    int N = el.Nodes();
    int ndpn = 2*(4+nsol);
    lm.resize(N*(ndpn+3));
    
    for (int i=0; i<N; ++i)
    {
        int n = el.m_node[i];
        FENode& node = m_pMesh->Node(n);
        
        vector<int>& id = node.m_ID;
        
        // first the displacement dofs
        lm[ndpn*i  ] = id[m_dofX];
        lm[ndpn*i+1] = id[m_dofY];
        lm[ndpn*i+2] = id[m_dofZ];
        
        // next the rotational dofs
        lm[ndpn*i+3] = id[m_dofU];
        lm[ndpn*i+4] = id[m_dofV];
        lm[ndpn*i+5] = id[m_dofW];
        
        // now the pressure dofs
        lm[ndpn*i+6] = id[m_dofP];
        lm[ndpn*i+7] = id[m_dofQ];
        
        // concentration dofs
        for (int k=0; k<nsol; ++k) {
            lm[ndpn*i+8+2*k] = id[m_dofC+m_pMat->GetSolute(k)->GetSoluteID()];
            lm[ndpn*i+9+2*k] = id[m_dofD+m_pMat->GetSolute(k)->GetSoluteID()];
        }
        
        // rigid rotational dofs
        // TODO: Do we really need this?
        lm[ndpn*N + 3*i  ] = id[m_dofRU];
        lm[ndpn*N + 3*i+1] = id[m_dofRV];
        lm[ndpn*N + 3*i+2] = id[m_dofRW];
    }
}

//-----------------------------------------------------------------------------
bool FEMultiphasicShellDomain::Initialize()
{
    // initialize base class
    FESSIShellDomain::Initialize();
    
    // error flag (set true on error)
    bool bmerr = false;
    
    // initialize local coordinate systems (can I do this elsewhere?)
    FEElasticMaterial* pme = m_pMat->GetElasticMaterial();
    for (size_t i=0; i<m_Elem.size(); ++i)
    {
        FEShellElement& el = m_Elem[i];
        for (int n=0; n<el.GaussPoints(); ++n) pme->SetLocalCoordinateSystem(el, n, *(el.GetMaterialPoint(n)));
    }
    
    // extract the initial concentrations of the solid-bound molecules
    const int nsbm = m_pMat->SBMs();
    vector<double> sbmr(nsbm, 0);
    for (int i = 0; i<nsbm; ++i) {
        sbmr[i] = m_pMat->GetSBM(i)->m_rho0;
    }
    
    for (int i = 0; i<(int)m_Elem.size(); ++i)
    {
        // get the solid element
        FEShellElement& el = m_Elem[i];
        
        // get the number of integration points
        int nint = el.GaussPoints();
        
        // loop over the integration points
        for (int n = 0; n<nint; ++n)
        {
            FEMaterialPoint& mp = *el.GetMaterialPoint(n);
            FESolutesMaterialPoint& ps = *(mp.ExtractData<FESolutesMaterialPoint>());
            
            ps.m_sbmr = sbmr;
            ps.m_sbmrp = sbmr;
            ps.m_sbmrhat.assign(nsbm, 0);
        }
    }

    // check for initially inverted shells
    for (int i=0; i<Elements(); ++i)
    {
        FEShellElement& el = Element(i);
        int nint = el.GaussPoints();
        for (int n=0; n<nint; ++n)
        {
            double J0 = detJ0(el, n);
            if (J0 <= 0)
            {
                felog.printf("**************************** E R R O R ****************************\n");
                felog.printf("Negative jacobian detected at integration point %d of element %d\n", n+1, el.GetID());
                felog.printf("Jacobian = %lg\n", J0);
                felog.printf("Did you use the right node numbering?\n");
                felog.printf("Nodes:");
                for (int l=0; l<el.Nodes(); ++l)
                {
                    felog.printf("%d", el.m_node[l]+1);
                    if (l+1 != el.Nodes()) felog.printf(","); else felog.printf("\n");
                }
                felog.printf("*******************************************************************\n\n");
                bmerr = true;
            }
        }
    }
    
    return (bmerr == false);
}

//-----------------------------------------------------------------------------
void FEMultiphasicShellDomain::Activate()
{
    const int nsol = m_pMat->Solutes();
    
    for (int i=0; i<Nodes(); ++i)
    {
        FENode& node = Node(i);
        if (node.HasFlags(FENode::EXCLUDE) == false)
        {
            if (node.m_rid < 0)
            {
                node.m_ID[m_dofX] = DOF_ACTIVE;
                node.m_ID[m_dofY] = DOF_ACTIVE;
                node.m_ID[m_dofZ] = DOF_ACTIVE;
                
                if (node.HasFlags(FENode::SHELL))
                {
                    node.m_ID[m_dofU] = DOF_ACTIVE;
                    node.m_ID[m_dofV] = DOF_ACTIVE;
                    node.m_ID[m_dofW] = DOF_ACTIVE;
                }
            }
            
            node.m_ID[m_dofP] = DOF_ACTIVE;
            for (int l=0; l<nsol; ++l)
            {
                int dofc = m_dofC + m_pMat->GetSolute(l)->GetSoluteID();
                node.m_ID[dofc] = DOF_ACTIVE;
            }
            
            if (node.HasFlags(FENode::SHELL)) {
                node.m_ID[m_dofQ] = DOF_ACTIVE;
                for (int l=0; l<nsol; ++l)
                {
                    int dofd = m_dofD + m_pMat->GetSolute(l)->GetSoluteID();
                    node.m_ID[dofd] = DOF_ACTIVE;
                }
            }
        }
    }
    
    const int nsbm = m_pMat->SBMs();
    
    const int NE = FEElement::MAX_NODES;
    double p0[NE], q0[NE];
    vector< vector<double> > c0(nsol, vector<double>(NE));
    vector< vector<double> > d0(nsol, vector<double>(NE));
    vector<int> sid(nsol);
    for (int j = 0; j<nsol; ++j) sid[j] = m_pMat->GetSolute(j)->GetSoluteID();
    FEMesh& m = *GetMesh();
    
    for (int i = 0; i<(int)m_Elem.size(); ++i)
    {
        // get the solid element
        FEShellElement& el = m_Elem[i];
        
        // get the number of nodes
        int neln = el.Nodes();
        // get initial values of fluid pressure and solute concentrations
        for (int i = 0; i<neln; ++i)
        {
            p0[i] = m.Node(el.m_node[i]).get(m_dofP);
            q0[i] = m.Node(el.m_node[i]).get(m_dofQ);
            for (int isol = 0; isol<nsol; ++isol) {
                c0[isol][i] = m.Node(el.m_node[i]).get(m_dofC + sid[isol]);
                d0[isol][i] = m.Node(el.m_node[i]).get(m_dofD + sid[isol]);
            }
        }
        
        // get the number of integration points
        int nint = el.GaussPoints();
        
        // loop over the integration points
        for (int n = 0; n<nint; ++n)
        {
            FEMaterialPoint& mp = *el.GetMaterialPoint(n);
            FEElasticMaterialPoint& pm = *(mp.ExtractData<FEElasticMaterialPoint>());
            FEBiphasicMaterialPoint& pt = *(mp.ExtractData<FEBiphasicMaterialPoint>());
            FESolutesMaterialPoint& ps = *(mp.ExtractData<FESolutesMaterialPoint>());
            
            // initialize effective fluid pressure, its gradient, and fluid flux
            pt.m_p = evaluate(el, p0, q0, n);
            pt.m_gradp = gradient(el, p0, q0, n);
            pt.m_w = m_pMat->FluidFlux(mp);
            
            // initialize multiphasic solutes
            ps.m_nsol = nsol;
            ps.m_nsbm = nsbm;
            
            // initialize effective solute concentrations
            for (int isol = 0; isol<nsol; ++isol) {
                ps.m_c[isol] = evaluate(el, c0[isol], d0[isol], n);
                ps.m_gradc[isol] = gradient(el, c0[isol], d0[isol], n);
            }
            
            ps.m_psi = m_pMat->ElectricPotential(mp);
            for (int isol = 0; isol<nsol; ++isol) {
                ps.m_ca[isol] = m_pMat->Concentration(mp, isol);
                ps.m_j[isol] = m_pMat->SoluteFlux(mp, isol);
                ps.m_crp[isol] = pm.m_J*m_pMat->Porosity(mp)*ps.m_ca[isol];
            }
            pt.m_pa = m_pMat->Pressure(mp);
            
            // initialize referential solid volume fraction
            pt.m_phi0 = m_pMat->SolidReferentialVolumeFraction(mp);
            
            // calculate FCD, current and stress
            ps.m_cF = m_pMat->FixedChargeDensity(mp);
            ps.m_Ie = m_pMat->CurrentDensity(mp);
            pm.m_s = m_pMat->Stress(mp);
        }
    }
}

//-----------------------------------------------------------------------------
void FEMultiphasicShellDomain::Reset()
{
    // reset base class
    FESSIShellDomain::Reset();
    
    const int nsol = m_pMat->Solutes();
    const int nsbm = m_pMat->SBMs();
    
    // extract the initial concentrations of the solid-bound molecules
    vector<double> sbmr(nsbm,0);
    for (int i=0; i<nsbm; ++i) {
        sbmr[i] = m_pMat->GetSBM(i)->m_rho0;
    }
    
    for (int i=0; i<(int) m_Elem.size(); ++i)
    {
        // get the solid element
        FEShellElement& el = m_Elem[i];
        
        // get the number of integration points
        int nint = el.GaussPoints();
        
        // loop over the integration points
        for (int n=0; n<nint; ++n)
        {
            FEMaterialPoint& mp = *el.GetMaterialPoint(n);
            FEBiphasicMaterialPoint& pt = *(mp.ExtractData<FEBiphasicMaterialPoint>());
            FESolutesMaterialPoint& ps = *(mp.ExtractData<FESolutesMaterialPoint>());
            
            // initialize referential solid volume fraction
            pt.m_phi0 = m_pMat->m_phi0;
            
            // initialize multiphasic solutes
            ps.m_nsol = nsol;
            ps.m_c.assign(nsol,0);
            ps.m_ca.assign(nsol,0);
            ps.m_crp.assign(nsol, 0);
            ps.m_gradc.assign(nsol,vec3d(0,0,0));
            ps.m_k.assign(nsol, 0);
            ps.m_dkdJ.assign(nsol, 0);
            ps.m_dkdc.resize(nsol, vector<double>(nsol,0));
            ps.m_j.assign(nsol,vec3d(0,0,0));
            ps.m_nsbm = nsbm;
            ps.m_sbmr = sbmr;
            ps.m_sbmrp = sbmr;
            ps.m_sbmrhat.assign(nsbm,0);
            
            // reset chemical reaction element data
            ps.m_cri.clear();
            ps.m_crd.clear();
            for (int j=0; j<m_pMat->Reactions(); ++j)
                m_pMat->GetReaction(j)->ResetElementData(mp);
        }
    }
}

//-----------------------------------------------------------------------------
void FEMultiphasicShellDomain::PreSolveUpdate(const FETimeInfo& timeInfo)
{
    FESSIShellDomain::PreSolveUpdate(timeInfo);
    
    const int NE = FEElement::MAX_NODES;
    vec3d x0[NE], xt[NE], r0, rt;
    FEMesh& m = *GetMesh();
    for (size_t iel=0; iel<m_Elem.size(); ++iel)
    {
        FEShellElement& el = m_Elem[iel];
        int neln = el.Nodes();
        for (int i=0; i<neln; ++i)
        {
            x0[i] = m.Node(el.m_node[i]).m_r0;
            xt[i] = m.Node(el.m_node[i]).m_rt;
        }
        
        int n = el.GaussPoints();
        for (int j=0; j<n; ++j)
        {
            r0 = el.Evaluate(x0, j);
            rt = el.Evaluate(xt, j);
            
            FEMaterialPoint& mp = *el.GetMaterialPoint(j);
            FEElasticMaterialPoint& pe = *mp.ExtractData<FEElasticMaterialPoint>();
            FEBiphasicMaterialPoint& pt = *(mp.ExtractData<FEBiphasicMaterialPoint>());
            FESolutesMaterialPoint& ps = *(mp.ExtractData<FESolutesMaterialPoint>());
            FEMultigenSBMMaterialPoint& pmg = *(mp.ExtractData<FEMultigenSBMMaterialPoint>());
            
            pe.m_r0 = r0;
            pe.m_rt = rt;
            
            pe.m_J = defgrad(el, pe.m_F, j);
            
            // reset determinant of solid deformation gradient at previous time
            pt.m_Jp = pe.m_J;
            
            // reset referential solid volume fraction at previous time
            pt.m_phi0p = pt.m_phi0;
            
            // reset referential actual solute concentration at previous time
            for (int j=0; j<m_pMat->Solutes(); ++j) {
                ps.m_crp[j] = pe.m_J*m_pMat->Porosity(mp)*ps.m_ca[j];
            }
            
            // reset referential solid-bound molecule concentrations at previous time
            for (int j=0; j<ps.m_nsbm; ++j) {
                ps.m_sbmrp[j] = ps.m_sbmr[j];
            }
            
            // reset generational referential solid-bound molecule concentrations at previous time
            if (&pmg) {
                for (int i=0; i<pmg.m_ngen; ++i) {
                    for (int j=0; j<ps.m_nsbm; ++j) {
                        pmg.m_gsbmrp[i][j] = pmg.m_gsbmr[i][j];
                    }
                }
            }
            
            // reset chemical reaction element data
            for (int j=0; j<m_pMat->Reactions(); ++j)
                m_pMat->GetReaction(j)->InitializeElementData(mp);
            
            mp.Update(timeInfo);
        }
    }
}

//-----------------------------------------------------------------------------
void FEMultiphasicShellDomain::InternalForces(FEGlobalVector& R)
{
    size_t NE = m_Elem.size();
    
    // get nodal DOFS
    int nsol = m_pMat->Solutes();
    int ndpn = 2*(4+nsol);
    
#pragma omp parallel for
    for (int i=0; i<NE; ++i)
    {
        // element force vector
        vector<double> fe;
        vector<int> lm;
        
        // get the element
        FEShellElement& el = m_Elem[i];
        
        // get the element force vector and initialize it to zero
        int ndof = ndpn*el.Nodes();
        fe.assign(ndof, 0);
        
        // calculate internal force vector
        ElementInternalForce(el, fe);
        
        // get the element's LM vector
        UnpackLM(el, lm);
        
        // assemble element 'fe'-vector into global R vector
        //#pragma omp critical
        R.Assemble(el.m_node, lm, fe);
    }
}

//-----------------------------------------------------------------------------
//! calculates the internal equivalent nodal forces for solid elements

void FEMultiphasicShellDomain::ElementInternalForce(FEShellElement& el, vector<double>& fe)
{
    int i, isol, n;
    
    // jacobian matrix, inverse jacobian matrix and determinants
    double Ji[3][3], detJt;
    
    vec3d gradM, gradMu, gradMw;
    double Mu, Mw;
    mat3ds s;
    
    const double* Mr, *Ms, *M;
    
    int nint = el.GaussPoints();
    int neln = el.Nodes();
    
    double*	gw = el.GaussWeights();
    double eta;
    
    const int nsol = m_pMat->Solutes();
    int ndpn = 2*(4+nsol);
    
    const int nreact = m_pMat->Reactions();
    
    double dt = GetFEModel()->GetTime().timeIncrement;
    
    vec3d gcnt[3];
    
    // repeat for all integration points
    for (n=0; n<nint; ++n)
    {
        FEMaterialPoint& mp = *el.GetMaterialPoint(n);
        FEElasticMaterialPoint& pt = *(mp.ExtractData<FEElasticMaterialPoint>());
        FEBiphasicMaterialPoint& bpt = *(mp.ExtractData<FEBiphasicMaterialPoint>());
        FESolutesMaterialPoint& spt = *(mp.ExtractData<FESolutesMaterialPoint>());
        
        // calculate the jacobian
        detJt = invjact(el, Ji, n);
        
        detJt *= gw[n];
        
        // get the stress vector for this integration point
        s = pt.m_s;
        
        eta = el.gt(n);
        
        Mr = el.Hr(n);
        Ms = el.Hs(n);
        M  = el.H(n);
        
        ContraBaseVectors(el, n, gcnt);
        
        // next we get the determinant
        double Jp = bpt.m_Jp;
        double J = pt.m_J;
        
        // and then finally
        double divv = ((J-Jp)/dt)/J;
        
        // get the flux
        vec3d& w = bpt.m_w;
        
        vector<vec3d> j(spt.m_j);
        vector<int> z(nsol);
        vector<double> kappa(spt.m_k);
        vec3d je(0,0,0);
        
        for (isol=0; isol<nsol; ++isol) {
            // get the charge number
            z[isol] = m_pMat->GetSolute(isol)->ChargeNumber();
            je += j[isol]*z[isol];
        }
        
        // evaluate the porosity, its derivative w.r.t. J, and its gradient
        double phiw = m_pMat->Porosity(mp);
        vector<double> chat(nsol,0);
        
        // get the solvent supply
        double phiwhat = 0;
        if (m_pMat->GetSolventSupply()) phiwhat = m_pMat->GetSolventSupply()->Supply(mp);
        
        // chemical reactions
        for (i=0; i<nreact; ++i) {
            FEChemicalReaction* pri = m_pMat->GetReaction(i);
            double zhat = pri->ReactionSupply(mp);
            phiwhat += phiw*pri->m_Vbar*zhat;
            for (isol=0; isol<nsol; ++isol)
                chat[isol] += phiw*zhat*pri->m_v[isol];
        }
        
        for (i=0; i<neln; ++i)
        {
            gradM = gcnt[0]*Mr[i] + gcnt[1]*Ms[i];
            gradMu = (gradM*(1+eta) + gcnt[2]*M[i])/2;
            gradMw = (gradM*(1-eta) - gcnt[2]*M[i])/2;
            Mu = (1+eta)/2*M[i];
            Mw = (1-eta)/2*M[i];
            
            // calculate internal force
            vec3d fu = s*gradMu;
            vec3d fw = s*gradMw;
            
            // the '-' sign is so that the internal forces get subtracted
            // from the global residual vector
            fe[ndpn*i  ] -= fu.x*detJt;
            fe[ndpn*i+1] -= fu.y*detJt;
            fe[ndpn*i+2] -= fu.z*detJt;
            fe[ndpn*i+3] -= fw.x*detJt;
            fe[ndpn*i+4] -= fw.y*detJt;
            fe[ndpn*i+5] -= fw.z*detJt;
            fe[ndpn*i+6] -= dt*(w*gradMu + (phiwhat - divv)*Mu)*detJt;
            fe[ndpn*i+7] -= dt*(w*gradMw + (phiwhat - divv)*Mw)*detJt;
            for (isol=0; isol<nsol; ++isol) {
                fe[ndpn*i+8+2*isol] -= dt*(gradMu*(j[isol]+je*m_pMat->m_penalty)
                                           + Mu*(chat[isol] - (phiw*spt.m_ca[isol] - spt.m_crp[isol]/J)/dt)
                                           )*detJt;
                fe[ndpn*i+9+2*isol] -= dt*(gradMw*(j[isol]+je*m_pMat->m_penalty)
                                           + Mw*(chat[isol] - (phiw*spt.m_ca[isol] - spt.m_crp[isol]/J)/dt)
                                           )*detJt;
            }
        }
    }
}

//-----------------------------------------------------------------------------
void FEMultiphasicShellDomain::InternalForcesSS(FEGlobalVector& R)
{
    size_t NE = m_Elem.size();
    
    // get nodal DOFS
    int nsol = m_pMat->Solutes();
    int ndpn = 2*(4+nsol);
    
#pragma omp parallel for
    for (int i=0; i<NE; ++i)
    {
        // element force vector
        vector<double> fe;
        vector<int> lm;
        
        // get the element
        FEShellElement& el = m_Elem[i];
        
        // get the element force vector and initialize it to zero
        int ndof = ndpn*el.Nodes();
        fe.assign(ndof, 0);
        
        // calculate internal force vector
        ElementInternalForceSS(el, fe);
        
        // get the element's LM vector
        UnpackLM(el, lm);
        
        // assemble element 'fe'-vector into global R vector
        //#pragma omp critical
        R.Assemble(el.m_node, lm, fe);
    }
}

//-----------------------------------------------------------------------------
//! calculates the internal equivalent nodal forces for solid elements

void FEMultiphasicShellDomain::ElementInternalForceSS(FEShellElement& el, vector<double>& fe)
{
    int i, isol, n;
    
    // jacobian matrix, inverse jacobian matrix and determinants
    double Ji[3][3], detJt;
    
    vec3d gradM, gradMu, gradMw;
    double Mu, Mw;
    mat3ds s;
    
    const double* Mr, *Ms, *M;
    
    int nint = el.GaussPoints();
    int neln = el.Nodes();
    
    double*	gw = el.GaussWeights();
    double eta;
    
    vec3d gcnt[3];
    
    const int nsol = m_pMat->Solutes();
    int ndpn = 2*(4+nsol);
    
    const int nreact = m_pMat->Reactions();
    
    double dt = GetFEModel()->GetTime().timeIncrement;
    
    // repeat for all integration points
    for (n=0; n<nint; ++n)
    {
        FEMaterialPoint& mp = *el.GetMaterialPoint(n);
        FEElasticMaterialPoint& pt = *(mp.ExtractData<FEElasticMaterialPoint>());
        FEBiphasicMaterialPoint& bpt = *(mp.ExtractData<FEBiphasicMaterialPoint>());
        FESolutesMaterialPoint& spt = *(mp.ExtractData<FESolutesMaterialPoint>());
        
        // calculate the jacobian
        detJt = invjact(el, Ji, n);
        
        detJt *= gw[n];
        
        // get the stress vector for this integration point
        s = pt.m_s;
        
        eta = el.gt(n);
        
        Mr = el.Hr(n);
        Ms = el.Hs(n);
        M  = el.H(n);
        
        ContraBaseVectors(el, n, gcnt);
        
        // get the flux
        vec3d& w = bpt.m_w;
        
        vector<vec3d> j(spt.m_j);
        vector<int> z(nsol);
        vector<double> kappa(spt.m_k);
        vec3d je(0,0,0);
        
        for (isol=0; isol<nsol; ++isol) {
            // get the charge number
            z[isol] = m_pMat->GetSolute(isol)->ChargeNumber();
            je += j[isol]*z[isol];
        }
        
        // evaluate the porosity, its derivative w.r.t. J, and its gradient
        double phiw = m_pMat->Porosity(mp);
        vector<double> chat(nsol,0);
        
        // get the solvent supply
        double phiwhat = 0;
        if (m_pMat->GetSolventSupply()) phiwhat = m_pMat->GetSolventSupply()->Supply(mp);
        
        // chemical reactions
        for (i=0; i<nreact; ++i) {
            FEChemicalReaction* pri = m_pMat->GetReaction(i);
            double zhat = pri->ReactionSupply(mp);
            phiwhat += phiw*pri->m_Vbar*zhat;
            for (isol=0; isol<nsol; ++isol)
                chat[isol] += phiw*zhat*pri->m_v[isol];
        }
        
        for (i=0; i<neln; ++i)
        {
            gradM = gcnt[0]*Mr[i] + gcnt[1]*Ms[i];
            gradMu = (gradM*(1+eta) + gcnt[2]*M[i])/2;
            gradMw = (gradM*(1-eta) - gcnt[2]*M[i])/2;
            Mu = (1+eta)/2*M[i];
            Mw = (1-eta)/2*M[i];
            
            // calculate internal force
            vec3d fu = s*gradMu;
            vec3d fw = s*gradMw;
            
            // the '-' sign is so that the internal forces get subtracted
            // from the global residual vector
            fe[ndpn*i  ] -= fu.x*detJt;
            fe[ndpn*i+1] -= fu.y*detJt;
            fe[ndpn*i+2] -= fu.z*detJt;
            fe[ndpn*i+3] -= fw.x*detJt;
            fe[ndpn*i+4] -= fw.y*detJt;
            fe[ndpn*i+5] -= fw.z*detJt;
            fe[ndpn*i+6] -= dt*(w*gradMu + Mu*phiwhat)*detJt;
            fe[ndpn*i+7] -= dt*(w*gradMw + Mw*phiwhat)*detJt;
            for (isol=0; isol<nsol; ++isol) {
                fe[ndpn*i+8+2*isol] -= dt*(gradMu*(j[isol]+je*m_pMat->m_penalty)
                                         + Mu*phiw*chat[isol]
                                         )*detJt;
                fe[ndpn*i+9+2*isol] -= dt*(gradMw*(j[isol]+je*m_pMat->m_penalty)
                                           + Mw*phiw*chat[isol]
                                           )*detJt;
            }
        }
    }
}

//-----------------------------------------------------------------------------
void FEMultiphasicShellDomain::StiffnessMatrix(FESolver* psolver, bool bsymm)
{
    const int nsol = m_pMat->Solutes();
    int ndpn = 2*(4+nsol);
    
    // repeat over all solid elements
    int NE = (int)m_Elem.size();
    
#pragma omp parallel for
    for (int iel=0; iel<NE; ++iel)
    {
        // element stiffness matrix
        matrix ke;
        vector<int> lm;
        
        FEShellElement& el = m_Elem[iel];
        UnpackLM(el, lm);
        
        // allocate stiffness matrix
        int neln = el.Nodes();
        int ndof = neln*ndpn;
        ke.resize(ndof, ndof);
        
        // calculate the element stiffness matrix
        ElementMultiphasicStiffness(el, ke, bsymm);
        
        // assemble element matrix in global stiffness matrix
#pragma omp critical
        psolver->AssembleStiffness(el.m_node, lm, ke);
    }
}

//-----------------------------------------------------------------------------
void FEMultiphasicShellDomain::StiffnessMatrixSS(FESolver* psolver, bool bsymm)
{
    const int nsol = m_pMat->Solutes();
    int ndpn = 2*(4+nsol);
    
    // repeat over all solid elements
    int NE = (int)m_Elem.size();
    
#pragma omp parallel for
    for (int iel=0; iel<NE; ++iel)
    {
        // element stiffness matrix
        matrix ke;
        vector<int> lm;
        
        FEShellElement& el = m_Elem[iel];
        UnpackLM(el, lm);
        
        // allocate stiffness matrix
        int neln = el.Nodes();
        int ndof = neln*ndpn;
        ke.resize(ndof, ndof);
        
        // calculate the element stiffness matrix
        ElementMultiphasicStiffnessSS(el, ke, bsymm);
        
        // assemble element matrix in global stiffness matrix
#pragma omp critical
        psolver->AssembleStiffness(el.m_node, lm, ke);
    }
}

//-----------------------------------------------------------------------------
//! calculates element stiffness matrix for element iel
//!
bool FEMultiphasicShellDomain::ElementMultiphasicStiffness(FEShellElement& el, matrix& ke, bool bsymm)
{
    int i, j, isol, jsol, n, ireact, isbm;
    
    int nint = el.GaussPoints();
    int neln = el.Nodes();
    
    const double* Mr, *Ms, *M;
    
    // jacobian
    double Ji[3][3], detJ;
    
    // Gradient of shape functions
    vector<vec3d> gradMu(neln), gradMw(neln);
    vector<double> Mu(neln), Mw(neln);
    vec3d gradM;
    double tmp;
    
    // gauss-weights
    double* gw = el.GaussWeights();
    double eta;
    
    vec3d gcnt[3];
    
    double dt = GetFEModel()->GetTime().timeIncrement;
    
    const int nsol = m_pMat->Solutes();
    int ndpn = 2*(4+nsol);
    
    const int nsbm   = m_pMat->SBMs();
    const int nreact = m_pMat->Reactions();
    
    // zero stiffness matrix
    ke.zero();
    
    // loop over gauss-points
    for (n=0; n<nint; ++n)
    {
        FEMaterialPoint& mp = *el.GetMaterialPoint(n);
        FEElasticMaterialPoint&  ept = *(mp.ExtractData<FEElasticMaterialPoint >());
        FEBiphasicMaterialPoint& ppt = *(mp.ExtractData<FEBiphasicMaterialPoint>());
        FESolutesMaterialPoint&  spt = *(mp.ExtractData<FESolutesMaterialPoint >());
        
        // calculate jacobian
        detJ = invjact(el, Ji, n)*gw[n];
        
        eta = el.gt(n);
        
        Mr = el.Hr(n);
        Ms = el.Hs(n);
        M  = el.H(n);
        
        ContraBaseVectors(el, n, gcnt);
        
        // calculate global gradient of shape functions
        for (i=0; i<neln; ++i)
        {
            // calculate global gradient of shape functions
            gradM = gcnt[0]*Mr[i] + gcnt[1]*Ms[i];
            gradMu[i] = (gradM*(1+eta) + gcnt[2]*M[i])/2;
            gradMw[i] = (gradM*(1-eta) - gcnt[2]*M[i])/2;
            Mu[i] = (1+eta)/2*M[i];
            Mw[i] = (1-eta)/2*M[i];
        }
        
        // get stress tensor
        mat3ds s = ept.m_s;
        
        // get elasticity tensor
        tens4ds C = m_pMat->Tangent(mp);
        
        // next we get the determinant
        double J = ept.m_J;
        
        // get the fluid flux and pressure gradient
        vec3d w = ppt.m_w;
        vec3d gradp = ppt.m_gradp;
        
        vector<double> c(spt.m_c);
        vector<vec3d> gradc(spt.m_gradc);
        vector<int> z(nsol);
        
        vector<double> kappa(spt.m_k);
        
        // get the charge number
        for (isol=0; isol<nsol; ++isol)
            z[isol] = m_pMat->GetSolute(isol)->ChargeNumber();
        
        vector<double> dkdJ(spt.m_dkdJ);
        vector< vector<double> > dkdc(spt.m_dkdc);
        vector< vector<double> > dkdr(spt.m_dkdr);
        vector< vector<double> > dkdJr(spt.m_dkdJr);
        vector< vector< vector<double> > > dkdrc(spt.m_dkdrc);
        
        // evaluate the porosity and its derivative
        double phiw = m_pMat->Porosity(mp);
        double phi0 = ppt.m_phi0;
        double phis = 1. - phiw;
        double dpdJ = phis/J;
        
        // evaluate the osmotic coefficient
        double osmc = m_pMat->GetOsmoticCoefficient()->OsmoticCoefficient(mp);
        
        // evaluate the permeability
        mat3ds K = m_pMat->GetPermeability()->Permeability(mp);
        tens4ds dKdE = m_pMat->GetPermeability()->Tangent_Permeability_Strain(mp);
        
        vector<mat3ds> dKdc(nsol);
        vector<mat3ds> D(nsol);
        vector<tens4ds> dDdE(nsol);
        vector< vector<mat3ds> > dDdc(nsol, vector<mat3ds>(nsol));
        vector<double> D0(nsol);
        vector< vector<double> > dD0dc(nsol, vector<double>(nsol));
        vector<double> dodc(nsol);
        vector<mat3ds> dTdc(nsol);
        vector<mat3ds> ImD(nsol);
        mat3dd I(1);
        
        // evaluate the solvent supply and its derivatives
        mat3ds Phie; Phie.zero();
        double Phip = 0;
        vector<double> Phic(nsol,0);
        vector<mat3ds> dchatde(nsol);
        if (m_pMat->GetSolventSupply()) {
            Phie = m_pMat->GetSolventSupply()->Tangent_Supply_Strain(mp);
            Phip = m_pMat->GetSolventSupply()->Tangent_Supply_Pressure(mp);
        }
        
        // chemical reactions
        for (i=0; i<nreact; ++i)
            Phie += m_pMat->GetReaction(i)->m_Vbar*(I*m_pMat->GetReaction(i)->ReactionSupply(mp)
                                                    +m_pMat->GetReaction(i)->Tangent_ReactionSupply_Strain(mp)*(J*phiw));
        
        for (isol=0; isol<nsol; ++isol) {
            // evaluate the permeability derivatives
            dKdc[isol] = m_pMat->GetPermeability()->Tangent_Permeability_Concentration(mp,isol);
            
            // evaluate the diffusivity tensor and its derivatives
            D[isol] = m_pMat->GetSolute(isol)->m_pDiff->Diffusivity(mp);
            dDdE[isol] = m_pMat->GetSolute(isol)->m_pDiff->Tangent_Diffusivity_Strain(mp);
            
            // evaluate the solute free diffusivity
            D0[isol] = m_pMat->GetSolute(isol)->m_pDiff->Free_Diffusivity(mp);
            
            // evaluate the derivative of the osmotic coefficient
            dodc[isol] = m_pMat->GetOsmoticCoefficient()->Tangent_OsmoticCoefficient_Concentration(mp,isol);
            
            // evaluate the stress tangent with concentration
            //			dTdc[isol] = pm->GetSolid()->Tangent_Concentration(mp,isol);
            dTdc[isol] = mat3ds(0,0,0,0,0,0);
            
            ImD[isol] = I-D[isol]/D0[isol];
            
            for (jsol=0; jsol<nsol; ++jsol) {
                dDdc[isol][jsol] = m_pMat->GetSolute(isol)->m_pDiff->Tangent_Diffusivity_Concentration(mp,jsol);
                dD0dc[isol][jsol] = m_pMat->GetSolute(isol)->m_pDiff->Tangent_Free_Diffusivity_Concentration(mp,jsol);
            }
            
            // evaluate the solvent supply tangent with concentration
            if (m_pMat->GetSolventSupply()) Phic[isol] = m_pMat->GetSolventSupply()->Tangent_Supply_Concentration(mp,isol);
            
            // chemical reactions
            dchatde[isol].zero();
            for (ireact=0; ireact<nreact; ++ireact) {
                dchatde[isol] += m_pMat->GetReaction(ireact)->m_v[isol]
                *(I*m_pMat->GetReaction(ireact)->ReactionSupply(mp)
                  +m_pMat->GetReaction(ireact)->Tangent_ReactionSupply_Strain(mp)*(J*phiw));
                Phic[isol] += phiw*m_pMat->GetReaction(ireact)->m_Vbar
                *m_pMat->GetReaction(ireact)->Tangent_ReactionSupply_Concentration(mp, isol);
            }
        }
        
        // Miscellaneous constants
        double R = m_pMat->m_Rgas;
        double T = m_pMat->m_Tabs;
        double penalty = m_pMat->m_penalty;
        
        // evaluate the effective permeability and its derivatives
        mat3ds Ki = K.inverse();
        mat3ds Ke(0,0,0,0,0,0);
        tens4ds G = dyad1s(Ki,I) - dyad4s(Ki,I)*2 - ddots(dyad2s(Ki),dKdE)*0.5;
        vector<mat3ds> Gc(nsol);
        vector<mat3ds> dKedc(nsol);
        for (isol=0; isol<nsol; ++isol) {
            Ke += ImD[isol]*(kappa[isol]*c[isol]/D0[isol]);
            G += dyad1s(ImD[isol],I)*(R*T*c[isol]*J/D0[isol]/2/phiw*(dkdJ[isol]-kappa[isol]/phiw*dpdJ))
            +(dyad1s(I) - dyad4s(I)*2 - dDdE[isol]/D0[isol])*(R*T*kappa[isol]*c[isol]/phiw/D0[isol]);
            Gc[isol] = ImD[isol]*(kappa[isol]/D0[isol]);
            for (jsol=0; jsol<nsol; ++jsol) {
                Gc[isol] += ImD[jsol]*(c[jsol]/D0[jsol]*(dkdc[jsol][isol]-kappa[jsol]/D0[jsol]*dD0dc[jsol][isol]))
                -(dDdc[jsol][isol]-D[jsol]*(dD0dc[jsol][isol]/D0[jsol])*(kappa[jsol]*c[jsol]/SQR(D0[jsol])));
            }
            Gc[isol] *= R*T/phiw;
        }
        Ke = (Ki + Ke*(R*T/phiw)).inverse();
        tens4ds dKedE = dyad1s(Ke,I) - 2*dyad4s(Ke,I) - ddots(dyad2s(Ke),G)*0.5;
        for (isol=0; isol<nsol; ++isol)
            dKedc[isol] = -Ke*(-Ki*dKdc[isol]*Ki + Gc[isol])*Ke;
        
        // calculate all the matrices
        vec3d vtmp,gp,qpu, qpw;
        vector<vec3d> gc(nsol),qcu(nsol),qcw(nsol),wc(nsol),wd(nsol),jce(nsol),jde(nsol);
        vector< vector<vec3d> > jc(nsol, vector<vec3d>(nsol));
        vector< vector<vec3d> > jd(nsol, vector<vec3d>(nsol));
        mat3d wu, ww, jue, jwe;
        vector<mat3d> ju(nsol), jw(nsol);
        vector< vector<double> > qcc(nsol, vector<double>(nsol));
        vector< vector<double> > qcd(nsol, vector<double>(nsol));
        vector< vector<double> > dchatdc(nsol, vector<double>(nsol));
        double sum;
        mat3ds De;
        for (i=0; i<neln; ++i)
        {
            for (j=0; j<neln; ++j)
            {
                // Kuu matrix
                mat3d Kuu = (mat3dd(gradMu[i]*(s*gradMu[j])) + vdotTdotv(gradMu[i], C, gradMu[j]))*detJ;
                mat3d Kuw = (mat3dd(gradMu[i]*(s*gradMw[j])) + vdotTdotv(gradMu[i], C, gradMw[j]))*detJ;
                mat3d Kwu = (mat3dd(gradMw[i]*(s*gradMu[j])) + vdotTdotv(gradMw[i], C, gradMu[j]))*detJ;
                mat3d Kww = (mat3dd(gradMw[i]*(s*gradMw[j])) + vdotTdotv(gradMw[i], C, gradMw[j]))*detJ;

                ke[ndpn*i  ][ndpn*j  ] += Kuu[0][0]; ke[ndpn*i  ][ndpn*j+1] += Kuu[0][1]; ke[ndpn*i  ][ndpn*j+2] += Kuu[0][2];
                ke[ndpn*i+1][ndpn*j  ] += Kuu[1][0]; ke[ndpn*i+1][ndpn*j+1] += Kuu[1][1]; ke[ndpn*i+1][ndpn*j+2] += Kuu[1][2];
                ke[ndpn*i+2][ndpn*j  ] += Kuu[2][0]; ke[ndpn*i+2][ndpn*j+1] += Kuu[2][1]; ke[ndpn*i+2][ndpn*j+2] += Kuu[2][2];
                
                ke[ndpn*i  ][ndpn*j+3] += Kuw[0][0]; ke[ndpn*i  ][ndpn*j+4] += Kuw[0][1]; ke[ndpn*i  ][ndpn*j+5] += Kuw[0][2];
                ke[ndpn*i+1][ndpn*j+3] += Kuw[1][0]; ke[ndpn*i+1][ndpn*j+4] += Kuw[1][1]; ke[ndpn*i+1][ndpn*j+5] += Kuw[1][2];
                ke[ndpn*i+2][ndpn*j+3] += Kuw[2][0]; ke[ndpn*i+2][ndpn*j+4] += Kuw[2][1]; ke[ndpn*i+2][ndpn*j+5] += Kuw[2][2];
                
                ke[ndpn*i+3][ndpn*j  ] += Kwu[0][0]; ke[ndpn*i+3][ndpn*j+1] += Kwu[0][1]; ke[ndpn*i+3][ndpn*j+2] += Kwu[0][2];
                ke[ndpn*i+4][ndpn*j  ] += Kwu[1][0]; ke[ndpn*i+4][ndpn*j+1] += Kwu[1][1]; ke[ndpn*i+4][ndpn*j+2] += Kwu[1][2];
                ke[ndpn*i+5][ndpn*j  ] += Kwu[2][0]; ke[ndpn*i+5][ndpn*j+1] += Kwu[2][1]; ke[ndpn*i+5][ndpn*j+2] += Kwu[2][2];
                
                ke[ndpn*i+3][ndpn*j+3] += Kww[0][0]; ke[ndpn*i+3][ndpn*j+4] += Kww[0][1]; ke[ndpn*i+3][ndpn*j+5] += Kww[0][2];
                ke[ndpn*i+4][ndpn*j+3] += Kww[1][0]; ke[ndpn*i+4][ndpn*j+4] += Kww[1][1]; ke[ndpn*i+4][ndpn*j+5] += Kww[1][2];
                ke[ndpn*i+5][ndpn*j+3] += Kww[2][0]; ke[ndpn*i+5][ndpn*j+4] += Kww[2][1]; ke[ndpn*i+5][ndpn*j+5] += Kww[2][2];
                
                // calculate the kpu matrix
                gp = vec3d(0,0,0);
                for (isol=0; isol<nsol; ++isol) gp += (D[isol]*gradc[isol])*(kappa[isol]/D0[isol]);
                gp = gradp+gp*(R*T);
                wu = vdotTdotv(-gp, dKedE, gradMu[j]);
                ww = vdotTdotv(-gp, dKedE, gradMw[j]);
                for (isol=0; isol<nsol; ++isol) {
                    wu += (((Ke*(D[isol]*gradc[isol])) & gradMu[j])*(J*dkdJ[isol] - kappa[isol])
                           +Ke*(2*kappa[isol]*(gradMu[j]*(D[isol]*gradc[isol]))))*(-R*T/D0[isol])
                    + (Ke*vdotTdotv(gradc[isol], dDdE[isol], gradMu[j]))*(-kappa[isol]*R*T/D0[isol]);
                    ww += (((Ke*(D[isol]*gradc[isol])) & gradMw[j])*(J*dkdJ[isol] - kappa[isol])
                           +Ke*(2*kappa[isol]*(gradMw[j]*(D[isol]*gradc[isol]))))*(-R*T/D0[isol])
                    + (Ke*vdotTdotv(gradc[isol], dDdE[isol], gradMw[j]))*(-kappa[isol]*R*T/D0[isol]);
                }
                qpu = -gradMu[j]*(1.0/dt);
                qpw = -gradMw[j]*(1.0/dt);
                vec3d kpu = (wu.transpose()*gradMu[i] + (qpu + Phie*gradMu[j])*Mu[i])*(detJ*dt);
                vec3d kpw = (ww.transpose()*gradMu[i] + (qpw + Phie*gradMw[j])*Mu[i])*(detJ*dt);
                vec3d kqu = (wu.transpose()*gradMw[i] + (qpu + Phie*gradMu[j])*Mw[i])*(detJ*dt);
                vec3d kqw = (ww.transpose()*gradMw[i] + (qpw + Phie*gradMw[j])*Mw[i])*(detJ*dt);
                ke[ndpn*i+6][ndpn*j  ] += kpu.x; ke[ndpn*i+6][ndpn*j+1] += kpu.y; ke[ndpn*i+6][ndpn*j+2] += kpu.z;
                ke[ndpn*i+6][ndpn*j+3] += kpw.x; ke[ndpn*i+6][ndpn*j+4] += kpw.y; ke[ndpn*i+6][ndpn*j+5] += kpw.z;
                ke[ndpn*i+7][ndpn*j  ] += kqu.x; ke[ndpn*i+7][ndpn*j+1] += kqu.y; ke[ndpn*i+7][ndpn*j+2] += kqu.z;
                ke[ndpn*i+7][ndpn*j+3] += kqw.x; ke[ndpn*i+7][ndpn*j+4] += kqw.y; ke[ndpn*i+7][ndpn*j+5] += kqw.z;
                
                // calculate the kup matrix
                vec3d kup = gradMu[i]*(-Mu[j]*detJ);
                vec3d kuq = gradMu[i]*(-Mw[j]*detJ);
                vec3d kwp = gradMw[i]*(-Mu[j]*detJ);
                vec3d kwq = gradMw[i]*(-Mw[j]*detJ);

                ke[ndpn*i  ][ndpn*j+6] += kup.x; ke[ndpn*i  ][ndpn*j+7] += kuq.x;
                ke[ndpn*i+1][ndpn*j+6] += kup.y; ke[ndpn*i+1][ndpn*j+7] += kuq.y;
                ke[ndpn*i+2][ndpn*j+6] += kup.z; ke[ndpn*i+2][ndpn*j+7] += kuq.z;
                
                ke[ndpn*i+3][ndpn*j+6] += kwp.x; ke[ndpn*i+3][ndpn*j+7] += kwq.x;
                ke[ndpn*i+4][ndpn*j+6] += kwp.y; ke[ndpn*i+4][ndpn*j+7] += kwq.y;
                ke[ndpn*i+5][ndpn*j+6] += kwp.z; ke[ndpn*i+5][ndpn*j+7] += kwq.z;
                
                // calculate the kpp matrix
                ke[ndpn*i+6][ndpn*j+6] += (Mu[i]*Mu[j]*Phip - gradMu[i]*(Ke*gradMu[j]))*(detJ*dt);
                ke[ndpn*i+6][ndpn*j+7] += (Mu[i]*Mw[j]*Phip - gradMu[i]*(Ke*gradMw[j]))*(detJ*dt);
                ke[ndpn*i+7][ndpn*j+6] += (Mw[i]*Mu[j]*Phip - gradMw[i]*(Ke*gradMu[j]))*(detJ*dt);
                ke[ndpn*i+7][ndpn*j+7] += (Mw[i]*Mw[j]*Phip - gradMw[i]*(Ke*gradMw[j]))*(detJ*dt);
                
                // calculate kcu matrix data
                jue.zero(); jwe.zero();
                De.zero();
                for (isol=0; isol<nsol; ++isol) {
                    gc[isol] = -gradc[isol]*phiw + w*c[isol]/D0[isol];
                    ju[isol] = ((D[isol]*gc[isol]) & gradMu[j])*(J*dkdJ[isol])
                    + vdotTdotv(gc[isol], dDdE[isol], gradMu[j])*kappa[isol]
                    + (((D[isol]*gradc[isol]) & gradMu[j])*(-phis)
                       +(D[isol]*((gradMu[j]*w)*2) - ((D[isol]*w) & gradMu[j]))*c[isol]/D0[isol]
                       )*kappa[isol]
                    +D[isol]*wu*(kappa[isol]*c[isol]/D0[isol]);
                    jw[isol] = ((D[isol]*gc[isol]) & gradMw[j])*(J*dkdJ[isol])
                    + vdotTdotv(gc[isol], dDdE[isol], gradMw[j])*kappa[isol]
                    + (((D[isol]*gradc[isol]) & gradMw[j])*(-phis)
                       +(D[isol]*((gradMw[j]*w)*2) - ((D[isol]*w) & gradMw[j]))*c[isol]/D0[isol]
                       )*kappa[isol]
                    +D[isol]*ww*(kappa[isol]*c[isol]/D0[isol]);
                    jue += ju[isol]*z[isol];
                    jwe += jw[isol]*z[isol];
                    De += D[isol]*(z[isol]*kappa[isol]*c[isol]/D0[isol]);
                    qcu[isol] = qpu*(c[isol]*(kappa[isol]+J*phiw*dkdJ[isol]));
                    qcw[isol] = qpw*(c[isol]*(kappa[isol]+J*phiw*dkdJ[isol]));
                    
                    // chemical reactions
                    for (ireact=0; ireact<nreact; ++ireact) {
                        double sum1 = 0;
                        double sum2 = 0;
                        for (isbm=0; isbm<nsbm; ++isbm) {
                            sum1 += m_pMat->SBMMolarMass(isbm)*m_pMat->GetReaction(ireact)->m_v[nsol+isbm]*
                            ((J-phi0)*dkdr[isol][isbm]-kappa[isol]/m_pMat->SBMDensity(isbm));
                            sum2 += m_pMat->SBMMolarMass(isbm)*m_pMat->GetReaction(ireact)->m_v[nsol+isbm]*
                            (dkdr[isol][isbm]+(J-phi0)*dkdJr[isol][isbm]-dkdJ[isol]/m_pMat->SBMDensity(isbm));
                        }
                        double zhat = m_pMat->GetReaction(ireact)->ReactionSupply(mp);
                        mat3dd zhatI(zhat);
                        mat3ds dzde = m_pMat->GetReaction(ireact)->Tangent_ReactionSupply_Strain(mp);
                        qcu[isol] -= ((zhatI+dzde*(J-phi0))*gradMu[j])*(sum1*c[isol])
                        +gradMu[j]*(c[isol]*(J-phi0)*sum2*zhat);
                        qcw[isol] -= ((zhatI+dzde*(J-phi0))*gradMw[j])*(sum1*c[isol])
                        +gradMw[j]*(c[isol]*(J-phi0)*sum2*zhat);
                    }
                }
                
                for (isol=0; isol<nsol; ++isol) {
                    
                    // calculate the kcu matrix
                    vec3d kcu = ((ju[isol]+jue*penalty).transpose()*gradMu[i]
                            + (qcu[isol] + dchatde[isol]*gradMu[j])*Mu[i])*(detJ*dt);
                    vec3d kcw = ((jw[isol]+jwe*penalty).transpose()*gradMu[i]
                                 + (qcw[isol] + dchatde[isol]*gradMw[j])*Mu[i])*(detJ*dt);
                    vec3d kdu = ((ju[isol]+jue*penalty).transpose()*gradMw[i]
                                 + (qcu[isol] + dchatde[isol]*gradMu[j])*Mw[i])*(detJ*dt);
                    vec3d kdw = ((jw[isol]+jwe*penalty).transpose()*gradMw[i]
                                 + (qcw[isol] + dchatde[isol]*gradMw[j])*Mw[i])*(detJ*dt);
                    ke[ndpn*i+8+2*isol][ndpn*j  ] += kcu.x; ke[ndpn*i+8+2*isol][ndpn*j+1] += kcu.y; ke[ndpn*i+8+2*isol][ndpn*j+2] += kcu.z;
                    ke[ndpn*i+8+2*isol][ndpn*j+3] += kcw.x; ke[ndpn*i+8+2*isol][ndpn*j+4] += kcw.y; ke[ndpn*i+8+2*isol][ndpn*j+5] += kcw.z;
                    ke[ndpn*i+9+2*isol][ndpn*j  ] += kdu.x; ke[ndpn*i+9+2*isol][ndpn*j+1] += kdu.y; ke[ndpn*i+9+2*isol][ndpn*j+2] += kdu.z;
                    ke[ndpn*i+9+2*isol][ndpn*j+3] += kdw.x; ke[ndpn*i+9+2*isol][ndpn*j+4] += kdw.y; ke[ndpn*i+9+2*isol][ndpn*j+5] += kdw.z;
                    
                    // calculate the kcp matrix
                    ke[ndpn*i+8+2*isol][ndpn*j+6] -= (gradMu[i]*(
                                                                 (D[isol]*(kappa[isol]*c[isol]/D0[isol])
                                                                  +De*penalty)
                                                                 *(Ke*gradMu[j])
                                                                 ))*(detJ*dt);
                    ke[ndpn*i+8+2*isol][ndpn*j+7] -= (gradMu[i]*(
                                                                 (D[isol]*(kappa[isol]*c[isol]/D0[isol])
                                                                  +De*penalty)
                                                                 *(Ke*gradMw[j])
                                                                 ))*(detJ*dt);
                    ke[ndpn*i+9+2*isol][ndpn*j+6] -= (gradMw[i]*(
                                                                 (D[isol]*(kappa[isol]*c[isol]/D0[isol])
                                                                  +De*penalty)
                                                                 *(Ke*gradMu[j])
                                                                 ))*(detJ*dt);
                    ke[ndpn*i+9+2*isol][ndpn*j+7] -= (gradMw[i]*(
                                                                 (D[isol]*(kappa[isol]*c[isol]/D0[isol])
                                                                  +De*penalty)
                                                                 *(Ke*gradMw[j])
                                                                 ))*(detJ*dt);
                    
                    // calculate the kuc matrix
                    sum = 0;
                    for (jsol=0; jsol<nsol; ++jsol)
                        sum += c[jsol]*(dodc[isol]*kappa[jsol]+osmc*dkdc[jsol][isol]);
                    vec3d kuc = (dTdc[isol]*gradMu[i] - gradMu[i]*(R*T*(osmc*kappa[isol]+sum)))*Mu[j]*detJ;
                    vec3d kud = (dTdc[isol]*gradMu[i] - gradMu[i]*(R*T*(osmc*kappa[isol]+sum)))*Mw[j]*detJ;
                    vec3d kwc = (dTdc[isol]*gradMw[i] - gradMw[i]*(R*T*(osmc*kappa[isol]+sum)))*Mu[j]*detJ;
                    vec3d kwd = (dTdc[isol]*gradMw[i] - gradMw[i]*(R*T*(osmc*kappa[isol]+sum)))*Mw[j]*detJ;

                    ke[ndpn*i  ][ndpn*j+8+2*isol] += kuc.x; ke[ndpn*i  ][ndpn*j+9+2*isol] += kud.x;
                    ke[ndpn*i+1][ndpn*j+8+2*isol] += kuc.y; ke[ndpn*i+1][ndpn*j+9+2*isol] += kud.y;
                    ke[ndpn*i+2][ndpn*j+8+2*isol] += kuc.z; ke[ndpn*i+2][ndpn*j+9+2*isol] += kud.z;
                    
                    ke[ndpn*i+3][ndpn*j+8+2*isol] += kwc.x; ke[ndpn*i+3][ndpn*j+9+2*isol] += kwd.x;
                    ke[ndpn*i+4][ndpn*j+8+2*isol] += kwc.y; ke[ndpn*i+4][ndpn*j+9+2*isol] += kwd.y;
                    ke[ndpn*i+5][ndpn*j+8+2*isol] += kwc.z; ke[ndpn*i+5][ndpn*j+9+2*isol] += kwd.z;
                    
                    // calculate the kpc matrix
                    vtmp = vec3d(0,0,0);
                    for (jsol=0; jsol<nsol; ++jsol)
                        vtmp += (D[jsol]*(dkdc[jsol][isol]-kappa[jsol]/D0[jsol]*dD0dc[jsol][isol])
                                 +dDdc[jsol][isol]*kappa[jsol])/D0[jsol]*gradc[jsol];
                    wc[isol] = (dKedc[isol]*gp)*(-Mu[j])
                    -Ke*((D[isol]*gradMu[j])*(kappa[isol]/D0[isol])+vtmp*Mu[j])*(R*T);
                    wd[isol] = (dKedc[isol]*gp)*(-Mw[j])
                    -Ke*((D[isol]*gradMw[j])*(kappa[isol]/D0[isol])+vtmp*Mw[j])*(R*T);

                    ke[ndpn*i+6][ndpn*j+8+2*isol] += (gradMu[i]*wc[isol])*(detJ*dt);
                    ke[ndpn*i+6][ndpn*j+9+2*isol] += (gradMu[i]*wd[isol])*(detJ*dt);
                    ke[ndpn*i+7][ndpn*j+8+2*isol] += (gradMw[i]*wc[isol])*(detJ*dt);
                    ke[ndpn*i+7][ndpn*j+9+2*isol] += (gradMw[i]*wd[isol])*(detJ*dt);
                    
                }
                
                // calculate data for the kcc matrix
                jce.assign(nsol, vec3d(0,0,0));
                jde.assign(nsol, vec3d(0,0,0));
                for (isol=0; isol<nsol; ++isol) {
                    for (jsol=0; jsol<nsol; ++jsol) {
                        if (jsol != isol) {
                            jc[isol][jsol] =
                            ((D[isol]*dkdc[isol][jsol]+dDdc[isol][jsol]*kappa[isol])*gc[isol])*Mu[j]
                            +(D[isol]*(w*(-Mu[j]*dD0dc[isol][jsol]/D0[isol])+wc[jsol]))*(kappa[isol]*c[isol]/D0[isol]);
                            jd[isol][jsol] =
                            ((D[isol]*dkdc[isol][jsol]+dDdc[isol][jsol]*kappa[isol])*gc[isol])*Mw[j]
                            +(D[isol]*(w*(-Mw[j]*dD0dc[isol][jsol]/D0[isol])+wd[jsol]))*(kappa[isol]*c[isol]/D0[isol]);
                            
                            qcc[isol][jsol] = -Mu[j]*phiw/dt*c[isol]*dkdc[isol][jsol];
                            qcd[isol][jsol] = -Mw[j]*phiw/dt*c[isol]*dkdc[isol][jsol];
                        }
                        else {
                            jc[isol][jsol] = (D[isol]*(gradMu[j]*(-phiw)+w*(Mu[j]/D0[isol])))*kappa[isol]
                            +((D[isol]*dkdc[isol][jsol]+dDdc[isol][jsol]*kappa[isol])*gc[isol])*Mu[j]
                            +(D[isol]*(w*(-Mu[j]*dD0dc[isol][jsol]/D0[isol])+wc[jsol]))*(kappa[isol]*c[isol]/D0[isol]);
                            jd[isol][jsol] = (D[isol]*(gradMw[j]*(-phiw)+w*(Mw[j]/D0[isol])))*kappa[isol]
                            +((D[isol]*dkdc[isol][jsol]+dDdc[isol][jsol]*kappa[isol])*gc[isol])*Mw[j]
                            +(D[isol]*(w*(-Mw[j]*dD0dc[isol][jsol]/D0[isol])+wd[jsol]))*(kappa[isol]*c[isol]/D0[isol]);
                            
                            qcc[isol][jsol] = -Mu[j]*phiw/dt*(c[isol]*dkdc[isol][jsol] + kappa[isol]);
                            qcd[isol][jsol] = -Mw[j]*phiw/dt*(c[isol]*dkdc[isol][jsol] + kappa[isol]);
                        }
                        jce[jsol] += jc[isol][jsol]*z[isol];
                        jde[jsol] += jd[isol][jsol]*z[isol];
                        
                        // chemical reactions
                        dchatdc[isol][jsol] = 0;
                        for (ireact=0; ireact<nreact; ++ireact) {
                            dchatdc[isol][jsol] += m_pMat->GetReaction(ireact)->m_v[isol]
                            *m_pMat->GetReaction(ireact)->Tangent_ReactionSupply_Concentration(mp,jsol);
                            double sum1 = 0;
                            double sum2 = 0;
                            for (isbm=0; isbm<nsbm; ++isbm) {
                                sum1 += m_pMat->SBMMolarMass(isbm)*m_pMat->GetReaction(ireact)->m_v[nsol+isbm]*
                                ((J-phi0)*dkdr[isol][isbm]-kappa[isol]/m_pMat->SBMDensity(isbm));
                                sum2 += m_pMat->SBMMolarMass(isbm)*m_pMat->GetReaction(ireact)->m_v[nsol+isbm]*
                                ((J-phi0)*dkdrc[isol][isbm][jsol]-dkdc[isol][jsol]/m_pMat->SBMDensity(isbm));
                            }
                            double zhat = m_pMat->GetReaction(ireact)->ReactionSupply(mp);
                            double dzdc = m_pMat->GetReaction(ireact)->Tangent_ReactionSupply_Concentration(mp, jsol);
                            if (jsol != isol) {
                                qcc[isol][jsol] -= Mu[j]*phiw*c[isol]*(dzdc*sum1+zhat*sum2);
                                qcd[isol][jsol] -= Mw[j]*phiw*c[isol]*(dzdc*sum1+zhat*sum2);
                            }
                            else {
                                qcc[isol][jsol] -= Mu[j]*phiw*((zhat+c[isol]*dzdc)*sum1+c[isol]*zhat*sum2);
                                qcd[isol][jsol] -= Mw[j]*phiw*((zhat+c[isol]*dzdc)*sum1+c[isol]*zhat*sum2);
                            }
                        }
                    }
                }
                
                // calculate the kcc matrix
                for (isol=0; isol<nsol; ++isol) {
                    for (jsol=0; jsol<nsol; ++jsol) {
                        ke[ndpn*i+8+2*isol][ndpn*j+8+2*jsol] += (gradMu[i]*(jc[isol][jsol]+jce[jsol]*penalty)
                                                             + Mu[i]*(qcc[isol][jsol]
                                                                     + Mu[j]*phiw*dchatdc[isol][jsol]))*(detJ*dt);
                        ke[ndpn*i+8+2*isol][ndpn*j+9+2*jsol] += (gradMu[i]*(jd[isol][jsol]+jde[jsol]*penalty)
                                                                 + Mu[i]*(qcd[isol][jsol]
                                                                          + Mw[j]*phiw*dchatdc[isol][jsol]))*(detJ*dt);
                        ke[ndpn*i+9+2*isol][ndpn*j+8+2*jsol] += (gradMw[i]*(jc[isol][jsol]+jce[jsol]*penalty)
                                                                 + Mw[i]*(qcc[isol][jsol]
                                                                          + Mu[j]*phiw*dchatdc[isol][jsol]))*(detJ*dt);
                        ke[ndpn*i+9+2*isol][ndpn*j+9+2*jsol] += (gradMw[i]*(jd[isol][jsol]+jde[jsol]*penalty)
                                                                 + Mw[i]*(qcd[isol][jsol]
                                                                          + Mw[j]*phiw*dchatdc[isol][jsol]))*(detJ*dt);
                    }
                }
            }
        }
    }
    
    // Enforce symmetry by averaging top-right and bottom-left corners of stiffness matrix
    if (bsymm) {
        for (i=0; i<ndpn*neln; ++i)
            for (j=i+1; j<ndpn*neln; ++j) {
                tmp = 0.5*(ke[i][j]+ke[j][i]);
                ke[i][j] = ke[j][i] = tmp;
            }
    }
    
    return true;
}

//-----------------------------------------------------------------------------
//! calculates element stiffness matrix for element iel
//! for steady-state response (zero solid velocity, zero time derivative of
//! solute concentration)
//!
bool FEMultiphasicShellDomain::ElementMultiphasicStiffnessSS(FEShellElement& el, matrix& ke, bool bsymm)
{
    int i, j, isol, jsol, n, ireact;
    
    int nint = el.GaussPoints();
    int neln = el.Nodes();
    
    const double* Mr, *Ms, *M;
    
    // jacobian
    double Ji[3][3], detJ;
    
    // Gradient of shape functions
    vector<vec3d> gradMu(neln), gradMw(neln);
    vector<double> Mu(neln), Mw(neln);
    vec3d gradM;
    double tmp;
    
    // gauss-weights
    double* gw = el.GaussWeights();
    double eta;
    
    vec3d gcnt[3];
    
    double dt = GetFEModel()->GetTime().timeIncrement;
    
    const int nsol = m_pMat->Solutes();
    int ndpn = 2*(4+nsol);
    
    const int nreact = m_pMat->Reactions();
    
    // zero stiffness matrix
    ke.zero();
    
    // loop over gauss-points
    for (n=0; n<nint; ++n)
    {
        FEMaterialPoint& mp = *el.GetMaterialPoint(n);
        FEElasticMaterialPoint&  ept = *(mp.ExtractData<FEElasticMaterialPoint >());
        FEBiphasicMaterialPoint& ppt = *(mp.ExtractData<FEBiphasicMaterialPoint>());
        FESolutesMaterialPoint&  spt = *(mp.ExtractData<FESolutesMaterialPoint >());
        
        // calculate jacobian
        detJ = invjact(el, Ji, n)*gw[n];
        
        eta = el.gt(n);
        
        Mr = el.Hr(n);
        Ms = el.Hs(n);
        M  = el.H(n);
        
        ContraBaseVectors(el, n, gcnt);
        
        // calculate global gradient of shape functions
        for (i=0; i<neln; ++i)
        {
            // calculate global gradient of shape functions
            gradM = gcnt[0]*Mr[i] + gcnt[1]*Ms[i];
            gradMu[i] = (gradM*(1+eta) + gcnt[2]*M[i])/2;
            gradMw[i] = (gradM*(1-eta) - gcnt[2]*M[i])/2;
            Mu[i] = (1+eta)/2*M[i];
            Mw[i] = (1-eta)/2*M[i];
        }
        
        // get stress tensor
        mat3ds s = ept.m_s;
        
        // get elasticity tensor
        tens4ds C = m_pMat->Tangent(mp);
        
        // next we get the determinant
        double J = ept.m_J;
        
        // get the fluid flux and pressure gradient
        vec3d w = ppt.m_w;
        vec3d gradp = ppt.m_gradp;
        
        vector<double> c(spt.m_c);
        vector<vec3d> gradc(spt.m_gradc);
        vector<int> z(nsol);
        
        vector<double> zz(nsol);
        vector<double> kappa(spt.m_k);
        
        // get the charge number
        for (isol=0; isol<nsol; ++isol)
            z[isol] = m_pMat->GetSolute(isol)->ChargeNumber();
        
        vector<double> dkdJ(spt.m_dkdJ);
        vector< vector<double> > dkdc(spt.m_dkdc);
        
        // evaluate the porosity and its derivative
        double phiw = m_pMat->Porosity(mp);
        double phis = 1. - phiw;
        double dpdJ = phis/J;
        
        // evaluate the osmotic coefficient
        double osmc = m_pMat->GetOsmoticCoefficient()->OsmoticCoefficient(mp);
        
        // evaluate the permeability
        mat3ds K = m_pMat->GetPermeability()->Permeability(mp);
        tens4ds dKdE = m_pMat->GetPermeability()->Tangent_Permeability_Strain(mp);
        
        vector<mat3ds> dKdc(nsol);
        vector<mat3ds> D(nsol);
        vector<tens4ds> dDdE(nsol);
        vector< vector<mat3ds> > dDdc(nsol, vector<mat3ds>(nsol));
        vector<double> D0(nsol);
        vector< vector<double> > dD0dc(nsol, vector<double>(nsol));
        vector<double> dodc(nsol);
        vector<mat3ds> dTdc(nsol);
        vector<mat3ds> ImD(nsol);
        mat3dd I(1);
        
        // evaluate the solvent supply and its derivatives
        mat3ds Phie; Phie.zero();
        double Phip = 0;
        vector<double> Phic(nsol,0);
        if (m_pMat->GetSolventSupply()) {
            Phie = m_pMat->GetSolventSupply()->Tangent_Supply_Strain(mp);
            Phip = m_pMat->GetSolventSupply()->Tangent_Supply_Pressure(mp);
        }
        
        // chemical reactions
        for (i=0; i<nreact; ++i)
            Phie += m_pMat->GetReaction(i)->m_Vbar*(I*m_pMat->GetReaction(i)->ReactionSupply(mp)
                                                    +m_pMat->GetReaction(i)->Tangent_ReactionSupply_Strain(mp)*(J*phiw));
        
        for (isol=0; isol<nsol; ++isol) {
            // evaluate the permeability derivatives
            dKdc[isol] = m_pMat->GetPermeability()->Tangent_Permeability_Concentration(mp,isol);
            
            // evaluate the diffusivity tensor and its derivatives
            D[isol] = m_pMat->GetSolute(isol)->m_pDiff->Diffusivity(mp);
            dDdE[isol] = m_pMat->GetSolute(isol)->m_pDiff->Tangent_Diffusivity_Strain(mp);
            
            // evaluate the solute free diffusivity
            D0[isol] = m_pMat->GetSolute(isol)->m_pDiff->Free_Diffusivity(mp);
            
            // evaluate the derivative of the osmotic coefficient
            dodc[isol] = m_pMat->GetOsmoticCoefficient()->Tangent_OsmoticCoefficient_Concentration(mp,isol);
            
            // evaluate the stress tangent with concentration
            //			dTdc[isol] = pm->GetSolid()->Tangent_Concentration(mp,isol);
            dTdc[isol] = mat3ds(0,0,0,0,0,0);
            
            ImD[isol] = I-D[isol]/D0[isol];
            
            for (jsol=0; jsol<nsol; ++jsol) {
                dDdc[isol][jsol] = m_pMat->GetSolute(isol)->m_pDiff->Tangent_Diffusivity_Concentration(mp,jsol);
                dD0dc[isol][jsol] = m_pMat->GetSolute(isol)->m_pDiff->Tangent_Free_Diffusivity_Concentration(mp,jsol);
            }
            
            // evaluate the solvent supply tangent with concentration
            if (m_pMat->GetSolventSupply()) Phic[isol] = m_pMat->GetSolventSupply()->Tangent_Supply_Concentration(mp,isol);
            
        }
        
        // Miscellaneous constants
        double R = m_pMat->m_Rgas;
        double T = m_pMat->m_Tabs;
        double penalty = m_pMat->m_penalty;
        
        // evaluate the effective permeability and its derivatives
        mat3ds Ki = K.inverse();
        mat3ds Ke(0,0,0,0,0,0);
        tens4ds G = dyad1s(Ki,I) - dyad4s(Ki,I)*2 - ddots(dyad2s(Ki),dKdE)*0.5;
        vector<mat3ds> Gc(nsol);
        vector<mat3ds> dKedc(nsol);
        for (isol=0; isol<nsol; ++isol) {
            Ke += ImD[isol]*(kappa[isol]*c[isol]/D0[isol]);
            G += dyad1s(ImD[isol],I)*(R*T*c[isol]*J/D0[isol]/2/phiw*(dkdJ[isol]-kappa[isol]/phiw*dpdJ))
            +(dyad1s(I) - dyad4s(I)*2 - dDdE[isol]/D0[isol])*(R*T*kappa[isol]*c[isol]/phiw/D0[isol]);
            Gc[isol] = ImD[isol]*(kappa[isol]/D0[isol]);
            for (jsol=0; jsol<nsol; ++jsol) {
                Gc[isol] += ImD[jsol]*(c[jsol]/D0[jsol]*(dkdc[jsol][isol]-kappa[jsol]/D0[jsol]*dD0dc[jsol][isol]))
                -(dDdc[jsol][isol]-D[jsol]*(dD0dc[jsol][isol]/D0[jsol])*(kappa[jsol]*c[jsol]/SQR(D0[jsol])));
            }
            Gc[isol] *= R*T/phiw;
        }
        Ke = (Ki + Ke*(R*T/phiw)).inverse();
        tens4ds dKedE = dyad1s(Ke,I) - 2*dyad4s(Ke,I) - ddots(dyad2s(Ke),G)*0.5;
        for (isol=0; isol<nsol; ++isol)
            dKedc[isol] = -Ke*(-Ki*dKdc[isol]*Ki + Gc[isol])*Ke;
        
        // calculate all the matrices
        vec3d vtmp,gp,qpu, qpw;
        vector<vec3d> gc(nsol),wc(nsol),wd(nsol),jce(nsol),jde(nsol);
        vector< vector<vec3d> > jc(nsol, vector<vec3d>(nsol));
        vector< vector<vec3d> > jd(nsol, vector<vec3d>(nsol));
        mat3d wu, ww, jue, jwe;
        vector<mat3d> ju(nsol), jw(nsol);
        vector< vector<double> > dchatdc(nsol, vector<double>(nsol));
        double sum;
        mat3ds De;
        for (i=0; i<neln; ++i)
        {
            for (j=0; j<neln; ++j)
            {
                // Kuu matrix
                mat3d Kuu = (mat3dd(gradMu[i]*(s*gradMu[j])) + vdotTdotv(gradMu[i], C, gradMu[j]))*detJ;
                mat3d Kuw = (mat3dd(gradMu[i]*(s*gradMw[j])) + vdotTdotv(gradMu[i], C, gradMw[j]))*detJ;
                mat3d Kwu = (mat3dd(gradMw[i]*(s*gradMu[j])) + vdotTdotv(gradMw[i], C, gradMu[j]))*detJ;
                mat3d Kww = (mat3dd(gradMw[i]*(s*gradMw[j])) + vdotTdotv(gradMw[i], C, gradMw[j]))*detJ;
                
                ke[ndpn*i  ][ndpn*j  ] += Kuu[0][0]; ke[ndpn*i  ][ndpn*j+1] += Kuu[0][1]; ke[ndpn*i  ][ndpn*j+2] += Kuu[0][2];
                ke[ndpn*i+1][ndpn*j  ] += Kuu[1][0]; ke[ndpn*i+1][ndpn*j+1] += Kuu[1][1]; ke[ndpn*i+1][ndpn*j+2] += Kuu[1][2];
                ke[ndpn*i+2][ndpn*j  ] += Kuu[2][0]; ke[ndpn*i+2][ndpn*j+1] += Kuu[2][1]; ke[ndpn*i+2][ndpn*j+2] += Kuu[2][2];
                
                ke[ndpn*i  ][ndpn*j+3] += Kuw[0][0]; ke[ndpn*i  ][ndpn*j+4] += Kuw[0][1]; ke[ndpn*i  ][ndpn*j+5] += Kuw[0][2];
                ke[ndpn*i+1][ndpn*j+3] += Kuw[1][0]; ke[ndpn*i+1][ndpn*j+4] += Kuw[1][1]; ke[ndpn*i+1][ndpn*j+5] += Kuw[1][2];
                ke[ndpn*i+2][ndpn*j+3] += Kuw[2][0]; ke[ndpn*i+2][ndpn*j+4] += Kuw[2][1]; ke[ndpn*i+2][ndpn*j+5] += Kuw[2][2];
                
                ke[ndpn*i+3][ndpn*j  ] += Kwu[0][0]; ke[ndpn*i+3][ndpn*j+1] += Kwu[0][1]; ke[ndpn*i+3][ndpn*j+2] += Kwu[0][2];
                ke[ndpn*i+4][ndpn*j  ] += Kwu[1][0]; ke[ndpn*i+4][ndpn*j+1] += Kwu[1][1]; ke[ndpn*i+4][ndpn*j+2] += Kwu[1][2];
                ke[ndpn*i+5][ndpn*j  ] += Kwu[2][0]; ke[ndpn*i+5][ndpn*j+1] += Kwu[2][1]; ke[ndpn*i+5][ndpn*j+2] += Kwu[2][2];
                
                ke[ndpn*i+3][ndpn*j+3] += Kww[0][0]; ke[ndpn*i+3][ndpn*j+4] += Kww[0][1]; ke[ndpn*i+3][ndpn*j+5] += Kww[0][2];
                ke[ndpn*i+4][ndpn*j+3] += Kww[1][0]; ke[ndpn*i+4][ndpn*j+4] += Kww[1][1]; ke[ndpn*i+4][ndpn*j+5] += Kww[1][2];
                ke[ndpn*i+5][ndpn*j+3] += Kww[2][0]; ke[ndpn*i+5][ndpn*j+4] += Kww[2][1]; ke[ndpn*i+5][ndpn*j+5] += Kww[2][2];
                
                // calculate the kpu matrix
                gp = vec3d(0,0,0);
                for (isol=0; isol<nsol; ++isol) gp += (D[isol]*gradc[isol])*(kappa[isol]/D0[isol]);
                gp = gradp+gp*(R*T);
                wu = vdotTdotv(-gp, dKedE, gradMu[j]);
                ww = vdotTdotv(-gp, dKedE, gradMw[j]);
                for (isol=0; isol<nsol; ++isol) {
                    wu += (((Ke*(D[isol]*gradc[isol])) & gradMu[j])*(J*dkdJ[isol] - kappa[isol])
                           +Ke*(2*kappa[isol]*(gradMu[j]*(D[isol]*gradc[isol]))))*(-R*T/D0[isol])
                    + (Ke*vdotTdotv(gradc[isol], dDdE[isol], gradMu[j]))*(-kappa[isol]*R*T/D0[isol]);
                    ww += (((Ke*(D[isol]*gradc[isol])) & gradMw[j])*(J*dkdJ[isol] - kappa[isol])
                           +Ke*(2*kappa[isol]*(gradMw[j]*(D[isol]*gradc[isol]))))*(-R*T/D0[isol])
                    + (Ke*vdotTdotv(gradc[isol], dDdE[isol], gradMw[j]))*(-kappa[isol]*R*T/D0[isol]);
                }
                qpu = Phie*gradMu[j];
                qpw = Phie*gradMw[j];
                vec3d kpu = (wu.transpose()*gradMu[i] + qpu*Mu[i])*(detJ*dt);
                vec3d kpw = (ww.transpose()*gradMu[i] + qpw*Mu[i])*(detJ*dt);
                vec3d kqu = (wu.transpose()*gradMw[i] + qpu*Mw[i])*(detJ*dt);
                vec3d kqw = (ww.transpose()*gradMw[i] + qpw*Mw[i])*(detJ*dt);
                ke[ndpn*i+6][ndpn*j  ] += kpu.x; ke[ndpn*i+6][ndpn*j+1] += kpu.y; ke[ndpn*i+6][ndpn*j+2] += kpu.z;
                ke[ndpn*i+6][ndpn*j+3] += kpw.x; ke[ndpn*i+6][ndpn*j+4] += kpw.y; ke[ndpn*i+6][ndpn*j+5] += kpw.z;
                ke[ndpn*i+7][ndpn*j  ] += kqu.x; ke[ndpn*i+7][ndpn*j+1] += kqu.y; ke[ndpn*i+7][ndpn*j+2] += kqu.z;
                ke[ndpn*i+7][ndpn*j+3] += kqw.x; ke[ndpn*i+7][ndpn*j+4] += kqw.y; ke[ndpn*i+7][ndpn*j+5] += kqw.z;
                
                // calculate the kup matrix
                vec3d kup = gradMu[i]*(-Mu[j]*detJ);
                vec3d kuq = gradMu[i]*(-Mw[j]*detJ);
                vec3d kwp = gradMw[i]*(-Mu[j]*detJ);
                vec3d kwq = gradMw[i]*(-Mw[j]*detJ);
                
                ke[ndpn*i  ][ndpn*j+6] += kup.x; ke[ndpn*i  ][ndpn*j+7] += kuq.x;
                ke[ndpn*i+1][ndpn*j+6] += kup.y; ke[ndpn*i+1][ndpn*j+7] += kuq.y;
                ke[ndpn*i+2][ndpn*j+6] += kup.z; ke[ndpn*i+2][ndpn*j+7] += kuq.z;
                
                ke[ndpn*i+3][ndpn*j+6] += kwp.x; ke[ndpn*i+3][ndpn*j+7] += kwq.x;
                ke[ndpn*i+4][ndpn*j+6] += kwp.y; ke[ndpn*i+4][ndpn*j+7] += kwq.y;
                ke[ndpn*i+5][ndpn*j+6] += kwp.z; ke[ndpn*i+5][ndpn*j+7] += kwq.z;
                
                // calculate the kpp matrix
                ke[ndpn*i+6][ndpn*j+6] += (Mu[i]*Mu[j]*Phip - gradMu[i]*(Ke*gradMu[j]))*(detJ*dt);
                ke[ndpn*i+6][ndpn*j+7] += (Mu[i]*Mw[j]*Phip - gradMu[i]*(Ke*gradMw[j]))*(detJ*dt);
                ke[ndpn*i+7][ndpn*j+6] += (Mw[i]*Mu[j]*Phip - gradMw[i]*(Ke*gradMu[j]))*(detJ*dt);
                ke[ndpn*i+7][ndpn*j+7] += (Mw[i]*Mw[j]*Phip - gradMw[i]*(Ke*gradMw[j]))*(detJ*dt);
                
                // calculate kcu matrix data
                jue.zero(); jwe.zero();
                De.zero();
                for (isol=0; isol<nsol; ++isol) {
                    gc[isol] = -gradc[isol]*phiw + w*c[isol]/D0[isol];
                    ju[isol] = ((D[isol]*gc[isol]) & gradMu[j])*(J*dkdJ[isol])
                    + vdotTdotv(gc[isol], dDdE[isol], gradMu[j])*kappa[isol]
                    + (((D[isol]*gradc[isol]) & gradMu[j])*(-phis)
                       +(D[isol]*((gradMu[j]*w)*2) - ((D[isol]*w) & gradMu[j]))*c[isol]/D0[isol]
                       )*kappa[isol]
                    +D[isol]*wu*(kappa[isol]*c[isol]/D0[isol]);
                    jw[isol] = ((D[isol]*gc[isol]) & gradMw[j])*(J*dkdJ[isol])
                    + vdotTdotv(gc[isol], dDdE[isol], gradMw[j])*kappa[isol]
                    + (((D[isol]*gradc[isol]) & gradMw[j])*(-phis)
                       +(D[isol]*((gradMw[j]*w)*2) - ((D[isol]*w) & gradMw[j]))*c[isol]/D0[isol]
                       )*kappa[isol]
                    +D[isol]*ww*(kappa[isol]*c[isol]/D0[isol]);
                    jue += ju[isol]*z[isol];
                    jwe += jw[isol]*z[isol];
                    De += D[isol]*(z[isol]*kappa[isol]*c[isol]/D0[isol]);
                }
                
                for (isol=0; isol<nsol; ++isol) {
                    
                    // calculate the kcu matrix
                    vec3d kcu = ((ju[isol]+jue*penalty).transpose()*gradMu[i])*(detJ*dt);
                    vec3d kcw = ((jw[isol]+jwe*penalty).transpose()*gradMu[i])*(detJ*dt);
                    vec3d kdu = ((ju[isol]+jue*penalty).transpose()*gradMw[i])*(detJ*dt);
                    vec3d kdw = ((jw[isol]+jwe*penalty).transpose()*gradMw[i])*(detJ*dt);
                    ke[ndpn*i+8+2*isol][ndpn*j  ] += kcu.x; ke[ndpn*i+8+2*isol][ndpn*j+1] += kcu.y; ke[ndpn*i+8+2*isol][ndpn*j+2] += kcu.z;
                    ke[ndpn*i+8+2*isol][ndpn*j+3] += kcw.x; ke[ndpn*i+8+2*isol][ndpn*j+4] += kcw.y; ke[ndpn*i+8+2*isol][ndpn*j+5] += kcw.z;
                    ke[ndpn*i+9+2*isol][ndpn*j  ] += kdu.x; ke[ndpn*i+9+2*isol][ndpn*j+1] += kdu.y; ke[ndpn*i+9+2*isol][ndpn*j+2] += kdu.z;
                    ke[ndpn*i+9+2*isol][ndpn*j+3] += kdw.x; ke[ndpn*i+9+2*isol][ndpn*j+4] += kdw.y; ke[ndpn*i+9+2*isol][ndpn*j+5] += kdw.z;
                    
                    // calculate the kcp matrix
                    ke[ndpn*i+8+2*isol][ndpn*j+6] -= (gradMu[i]*(
                                                                 (D[isol]*(kappa[isol]*c[isol]/D0[isol])
                                                                  +De*penalty)
                                                                 *(Ke*gradMu[j])
                                                                 ))*(detJ*dt);
                    ke[ndpn*i+8+2*isol][ndpn*j+7] -= (gradMu[i]*(
                                                                 (D[isol]*(kappa[isol]*c[isol]/D0[isol])
                                                                  +De*penalty)
                                                                 *(Ke*gradMw[j])
                                                                 ))*(detJ*dt);
                    ke[ndpn*i+9+2*isol][ndpn*j+6] -= (gradMw[i]*(
                                                                 (D[isol]*(kappa[isol]*c[isol]/D0[isol])
                                                                  +De*penalty)
                                                                 *(Ke*gradMu[j])
                                                                 ))*(detJ*dt);
                    ke[ndpn*i+9+2*isol][ndpn*j+7] -= (gradMw[i]*(
                                                                 (D[isol]*(kappa[isol]*c[isol]/D0[isol])
                                                                  +De*penalty)
                                                                 *(Ke*gradMw[j])
                                                                 ))*(detJ*dt);
                    
                    // calculate the kuc matrix
                    sum = 0;
                    for (jsol=0; jsol<nsol; ++jsol)
                        sum += c[jsol]*(dodc[isol]*kappa[jsol]+osmc*dkdc[jsol][isol]);
                    vec3d kuc = (dTdc[isol]*gradMu[i] - gradMu[i]*(R*T*(osmc*kappa[isol]+sum)))*Mu[j]*detJ;
                    vec3d kud = (dTdc[isol]*gradMu[i] - gradMu[i]*(R*T*(osmc*kappa[isol]+sum)))*Mw[j]*detJ;
                    vec3d kwc = (dTdc[isol]*gradMw[i] - gradMw[i]*(R*T*(osmc*kappa[isol]+sum)))*Mu[j]*detJ;
                    vec3d kwd = (dTdc[isol]*gradMw[i] - gradMw[i]*(R*T*(osmc*kappa[isol]+sum)))*Mw[j]*detJ;
                    
                    ke[ndpn*i  ][ndpn*j+8+2*isol] += kuc.x; ke[ndpn*i  ][ndpn*j+9+2*isol] += kud.x;
                    ke[ndpn*i+1][ndpn*j+8+2*isol] += kuc.y; ke[ndpn*i+1][ndpn*j+9+2*isol] += kud.y;
                    ke[ndpn*i+2][ndpn*j+8+2*isol] += kuc.z; ke[ndpn*i+2][ndpn*j+9+2*isol] += kud.z;
                    
                    ke[ndpn*i+3][ndpn*j+8+2*isol] += kwc.x; ke[ndpn*i+3][ndpn*j+9+2*isol] += kwd.x;
                    ke[ndpn*i+4][ndpn*j+8+2*isol] += kwc.y; ke[ndpn*i+4][ndpn*j+9+2*isol] += kwd.y;
                    ke[ndpn*i+5][ndpn*j+8+2*isol] += kwc.z; ke[ndpn*i+5][ndpn*j+9+2*isol] += kwd.z;
                    
                    // calculate the kpc matrix
                    vtmp = vec3d(0,0,0);
                    for (jsol=0; jsol<nsol; ++jsol)
                        vtmp += (D[jsol]*(dkdc[jsol][isol]-kappa[jsol]/D0[jsol]*dD0dc[jsol][isol])
                                 +dDdc[jsol][isol]*kappa[jsol])/D0[jsol]*gradc[jsol];
                    wc[isol] = (dKedc[isol]*gp)*(-Mu[j])
                    -Ke*((D[isol]*gradMu[j])*(kappa[isol]/D0[isol])+vtmp*Mu[j])*(R*T);
                    wd[isol] = (dKedc[isol]*gp)*(-Mw[j])
                    -Ke*((D[isol]*gradMw[j])*(kappa[isol]/D0[isol])+vtmp*Mw[j])*(R*T);
                    
                    ke[ndpn*i+6][ndpn*j+8+2*isol] += (gradMu[i]*wc[isol])*(detJ*dt);
                    ke[ndpn*i+6][ndpn*j+9+2*isol] += (gradMu[i]*wd[isol])*(detJ*dt);
                    ke[ndpn*i+7][ndpn*j+8+2*isol] += (gradMw[i]*wc[isol])*(detJ*dt);
                    ke[ndpn*i+7][ndpn*j+9+2*isol] += (gradMw[i]*wd[isol])*(detJ*dt);
                    
                }
                
                // calculate data for the kcc matrix
                jce.assign(nsol, vec3d(0,0,0));
                jde.assign(nsol, vec3d(0,0,0));
                for (isol=0; isol<nsol; ++isol) {
                    for (jsol=0; jsol<nsol; ++jsol) {
                        if (jsol != isol) {
                            jc[isol][jsol] =
                            ((D[isol]*dkdc[isol][jsol]+dDdc[isol][jsol]*kappa[isol])*gc[isol])*Mu[j]
                            +(D[isol]*(w*(-Mu[j]*dD0dc[isol][jsol]/D0[isol])+wc[jsol]))*(kappa[isol]*c[isol]/D0[isol]);
                            jd[isol][jsol] =
                            ((D[isol]*dkdc[isol][jsol]+dDdc[isol][jsol]*kappa[isol])*gc[isol])*Mw[j]
                            +(D[isol]*(w*(-Mw[j]*dD0dc[isol][jsol]/D0[isol])+wd[jsol]))*(kappa[isol]*c[isol]/D0[isol]);
                        }
                        else {
                            jc[isol][jsol] = (D[isol]*(gradMu[j]*(-phiw)+w*(Mu[j]/D0[isol])))*kappa[isol]
                            +((D[isol]*dkdc[isol][jsol]+dDdc[isol][jsol]*kappa[isol])*gc[isol])*Mu[j]
                            +(D[isol]*(w*(-Mu[j]*dD0dc[isol][jsol]/D0[isol])+wc[jsol]))*(kappa[isol]*c[isol]/D0[isol]);
                            jd[isol][jsol] = (D[isol]*(gradMw[j]*(-phiw)+w*(Mw[j]/D0[isol])))*kappa[isol]
                            +((D[isol]*dkdc[isol][jsol]+dDdc[isol][jsol]*kappa[isol])*gc[isol])*Mw[j]
                            +(D[isol]*(w*(-Mw[j]*dD0dc[isol][jsol]/D0[isol])+wd[jsol]))*(kappa[isol]*c[isol]/D0[isol]);
                        }
                        jce[jsol] += jc[isol][jsol]*z[isol];
                        jde[jsol] += jd[isol][jsol]*z[isol];
                        
                        // chemical reactions
                        dchatdc[isol][jsol] = 0;
                        for (ireact=0; ireact<nreact; ++ireact)
                            dchatdc[isol][jsol] += m_pMat->GetReaction(ireact)->m_v[isol]
                            *m_pMat->GetReaction(ireact)->Tangent_ReactionSupply_Concentration(mp,jsol);
                    }
                }
                
                // calculate the kcc matrix
                for (isol=0; isol<nsol; ++isol) {
                    for (jsol=0; jsol<nsol; ++jsol) {
                        ke[ndpn*i+8+2*isol][ndpn*j+8+2*jsol] += (gradMu[i]*(jc[isol][jsol]+jce[jsol]*penalty)
                                                                 + Mu[i]*Mu[j]*phiw*dchatdc[isol][jsol])*(detJ*dt);
                        ke[ndpn*i+8+2*isol][ndpn*j+9+2*jsol] += (gradMu[i]*(jd[isol][jsol]+jde[jsol]*penalty)
                                                                 + Mu[i]*Mw[j]*phiw*dchatdc[isol][jsol])*(detJ*dt);
                        ke[ndpn*i+9+2*isol][ndpn*j+8+2*jsol] += (gradMw[i]*(jc[isol][jsol]+jce[jsol]*penalty)
                                                                 + Mw[i]*Mu[j]*phiw*dchatdc[isol][jsol])*(detJ*dt);
                        ke[ndpn*i+9+2*isol][ndpn*j+9+2*jsol] += (gradMw[i]*(jd[isol][jsol]+jde[jsol]*penalty)
                                                                 + Mw[i]*Mw[j]*phiw*dchatdc[isol][jsol])*(detJ*dt);
                    }
                }
            }
        }
    }
    
    // Enforce symmetry by averaging top-right and bottom-left corners of stiffness matrix
    if (bsymm) {
        for (i=0; i<ndpn*neln; ++i)
            for (j=i+1; j<ndpn*neln; ++j) {
                tmp = 0.5*(ke[i][j]+ke[j][i]);
                ke[i][j] = ke[j][i] = tmp;
            }
    }
    
    return true;
}

//-----------------------------------------------------------------------------
void FEMultiphasicShellDomain::Update(const FETimeInfo& tp)
{
    FEModel& fem = *GetFEModel();
    bool berr = false;
    int NE = (int) m_Elem.size();
    double dt = fem.GetTime().timeIncrement;
#pragma omp parallel for shared(NE, berr)
    for (int i=0; i<NE; ++i)
    {
        try
        {
            UpdateElementStress(i, dt);
        }
        catch (NegativeJacobian e)
        {
#pragma omp critical
            {
                berr = true;
                if (NegativeJacobian::m_boutput) e.print();
            }
        }
    }
    
    // if we encountered an error, we request a running restart
    if (berr)
    {
        if (NegativeJacobian::m_boutput == false) felog.printbox("ERROR", "Negative jacobian was detected.");
        throw DoRunningRestart();
    }
}

//-----------------------------------------------------------------------------
void FEMultiphasicShellDomain::UpdateElementStress(int iel, double dt)
{
    int j, k, n;
    int nint, neln;
    double* gw;
    vec3d r0[FEElement::MAX_NODES];
    vec3d rt[FEElement::MAX_NODES];
    double pn[FEElement::MAX_NODES], qn[FEElement::MAX_NODES];
    
    FEMesh& mesh = *m_pMesh;
    
    // get the multiphasic material
    FEMultiphasic* pmb = m_pMat;
    const int nsol = (int)pmb->Solutes();
    vector< vector<double> > cn(nsol, vector<double>(FEElement::MAX_NODES));
    vector< vector<double> > dn(nsol, vector<double>(FEElement::MAX_NODES));
    vector<int> sid(nsol);
    for (j=0; j<nsol; ++j) sid[j] = pmb->GetSolute(j)->GetSoluteID();
    
    // get the solid element
    FEShellElement& el = m_Elem[iel];
    
    // get the number of integration points
    nint = el.GaussPoints();
    
    // get the number of nodes
    neln = el.Nodes();
    
    // get the integration weights
    gw = el.GaussWeights();
    
    // get the nodal data
    for (j=0; j<neln; ++j)
    {
        r0[j] = mesh.Node(el.m_node[j]).m_r0;
        rt[j] = mesh.Node(el.m_node[j]).m_rt;
        pn[j] = mesh.Node(el.m_node[j]).get(m_dofP);
        qn[j] = mesh.Node(el.m_node[j]).get(m_dofQ);
        for (k=0; k<nsol; ++k) {
            cn[k][j] = mesh.Node(el.m_node[j]).get(m_dofC + sid[k]);
            dn[k][j] = mesh.Node(el.m_node[j]).get(m_dofD + sid[k]);
        }
    }
    
    // loop over the integration points and calculate
    // the stress at the integration point
    for (n=0; n<nint; ++n)
    {
        FEMaterialPoint& mp = *el.GetMaterialPoint(n);
        FEElasticMaterialPoint& pt = *(mp.ExtractData<FEElasticMaterialPoint>());
        
        // material point coordinates
        // TODO: I'm not entirly happy with this solution
        //		 since the material point coordinates are used by most materials.
        pt.m_r0 = el.Evaluate(r0, n);
        pt.m_rt = el.Evaluate(rt, n);
        
        // get the deformation gradient and determinant
        pt.m_J = defgrad(el, pt.m_F, n);
        
        // multiphasic material point data
        FEBiphasicMaterialPoint& ppt = *(mp.ExtractData<FEBiphasicMaterialPoint>());
        FESolutesMaterialPoint& spt = *(mp.ExtractData<FESolutesMaterialPoint>());
        
        // update SBM referential densities
        pmb->UpdateSolidBoundMolecules(mp, dt);
        
        // evaluate referential solid volume fraction
        ppt.m_phi0 = pmb->SolidReferentialVolumeFraction(mp);
        
        // evaluate fluid pressure at gauss-point
        ppt.m_p = evaluate(el, pn, qn, n);
        
        // calculate the gradient of p at gauss-point
        ppt.m_gradp = gradient(el, pn, qn, n);
        
        for (k=0; k<nsol; ++k) {
            // evaluate effective solute concentrations at gauss-point
            spt.m_c[k] = evaluate(el, cn[k], dn[k], n);
            // calculate the gradient of c at gauss-point
            spt.m_gradc[k] = gradient(el, cn[k], dn[k], n);
        }
        
        // update the fluid and solute fluxes
        // and evaluate the actual fluid pressure and solute concentration
        ppt.m_w = pmb->FluidFlux(mp);
        spt.m_psi = pmb->ElectricPotential(mp);
        for (k=0; k<nsol; ++k) {
            spt.m_ca[k] = pmb->Concentration(mp,k);
            spt.m_j[k] = pmb->SoluteFlux(mp,k);
        }
        ppt.m_pa = pmb->Pressure(mp);
        spt.m_cF = pmb->FixedChargeDensity(mp);
        spt.m_Ie = pmb->CurrentDensity(mp);
        pmb->PartitionCoefficientFunctions(mp, spt.m_k, spt.m_dkdJ, spt.m_dkdc,
                                           spt.m_dkdr, spt.m_dkdJr, spt.m_dkdrc);
        // evaluate the stress
        pt.m_s = pmb->Stress(mp);
        
        // evaluate the referential solid density
        spt.m_rhor = pmb->SolidReferentialApparentDensity(mp);
        
        // update chemical reaction element data
        for (int j=0; j<m_pMat->Reactions(); ++j)
            pmb->GetReaction(j)->UpdateElementData(mp);
        
    }
}
