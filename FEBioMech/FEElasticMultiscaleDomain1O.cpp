#include "stdafx.h"
#include "FEElasticMultiscaleDomain1O.h"
#include "FEMicroMaterial.h"
#include "FECore/mat3d.h"
#include "FECore/tens6d.h"
#include <FECore/log.h>

//-----------------------------------------------------------------------------
//! constructor
FEElasticMultiscaleDomain1O::FEElasticMultiscaleDomain1O(FEModel* pfem) : FEElasticSolidDomain(pfem)
{
}

//-----------------------------------------------------------------------------
//! intialize domain
bool FEElasticMultiscaleDomain1O::Initialize(FEModel& fem)
{
	if (FEElasticSolidDomain::Initialize(fem) == false) return false;

	// get the material
	FEMicroMaterial* pmat = dynamic_cast<FEMicroMaterial*>(m_pMat);
	if (m_pMat == 0) return false;

	// get the master RVE
	FEModel& rve = pmat->m_mrve;

	// loop over all elements
	for (size_t i=0; i<m_Elem.size(); ++i)
	{
		FESolidElement& el = m_Elem[i];
		int nint = el.GaussPoints();
		for (int j=0; j<nint; ++j) 
		{
			FEMaterialPoint& mp = *el.GetMaterialPoint(j);
			FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
			FEMicroMaterialPoint& mmpt = *mp.ExtractData<FEMicroMaterialPoint>();

			// create the material point RVEs
			mmpt.m_F_prev = pt.m_F;	// TODO: I think I can remove this line
			mmpt.m_rve.CopyFrom(rve);
			mmpt.m_rve.Init();
		}
	}

	// create the probes
	int NP = pmat->Probes();
	for (int i=0; i<NP; ++i)
	{
		FEMicroProbe& p = pmat->Probe(i);
		FEElement* pel = FindElementFromID(p.m_neid);
		if (pel)
		{
			int nint = pel->GaussPoints();
			int ngp = p.m_ngp - 1;
			if ((ngp>=0)&&(ngp<nint))
			{
				FEMaterialPoint& mp = *pel->GetMaterialPoint(ngp);
				FEMicroMaterialPoint& mmpt = *mp.ExtractData<FEMicroMaterialPoint>();
				FERVEProbe* prve = new FERVEProbe(fem, mmpt.m_rve, p.m_szfile);
			}
			else return fecore_error("Invalid gausspt number for micro-probe %d in material %d (%s)", i+1, m_pMat->GetID(), m_pMat->GetName());
		}
		else return fecore_error("Invalid Element ID for micro probe %d in material %d (%s)", i+1, m_pMat->GetID(), m_pMat->GetName());
	}

	return true;
}
