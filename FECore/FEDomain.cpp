#include "stdafx.h"
#include "FEDomain.h"
#include "FEMaterial.h"

//-----------------------------------------------------------------------------
FEDomain::FEDomain(int ntype, int nclass, FEMesh* pm) : m_pMesh(pm), m_ntype(ntype), m_nclass(nclass) {}

//-----------------------------------------------------------------------------
FEDomain::~FEDomain() {}

//-----------------------------------------------------------------------------
FEElement* FEDomain::FindElementFromID(int nid)
{
	for (int i=0; i<Elements(); ++i)
	{
		FEElement& el = ElementRef(i);
		if (el.m_nID == nid) return &el;
	}

	return 0;
}

//-----------------------------------------------------------------------------
void FEDomain::InitMaterialPointData()
{
	FEMaterial* pmat = GetMaterial();

	for (int i=0; i<Elements(); ++i)
	{
		FEElement& el = ElementRef(i);
		for (int k=0; k<el.GaussPoints(); ++k) el.SetMaterialPointData(pmat->CreateMaterialPointData(), k);
	}
}

//-----------------------------------------------------------------------------
void FEDomain::SetMatID(int mid)
{
	for (int i=0; i<Elements(); ++i) ElementRef(i).SetMatID(mid);
}
