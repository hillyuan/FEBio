/*This file is part of the FEBio source code and is licensed under the MIT license
listed below.

See Copyright-FEBio.txt for details.

Copyright (c) 2021 University of Utah, The Trustees of Columbia University in
the City of New York, and others.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.*/



#include "FEFiberMaterialPoint.h"
#include "stdafx.h"
#include "FEElasticMaterial.h"
#include <FECore/DumpStream.h>

//-----------------------------------------------------------------------------
FEMaterialPoint* FEFiberMaterialPoint::Copy()
{
    FEFiberMaterialPoint* pt = new FEFiberMaterialPoint(*this);
    if (m_pNext) pt->m_pNext = m_pNext->Copy();
    return pt;
}

//-----------------------------------------------------------------------------
void FEFiberMaterialPoint::Init()
{
    // initialize data to identity
    m_Us = mat3dd(1);
    m_bUs = false;
    
    // don't forget to intialize the nested data
    FEMaterialPoint::Init();
}

//-----------------------------------------------------------------------------
void FEFiberMaterialPoint::Serialize(DumpStream& ar)
{
    FEMaterialPoint::Serialize(ar);
    ar & m_Us & m_bUs;
}

//-----------------------------------------------------------------------------
vec3d FEFiberMaterialPoint::FiberPreStretch(const vec3d a0)
{
    // account for prior deformation in multigenerational formulation
    if (m_bUs) {
        vec3d a = (m_Us*a0);
        a.unit();
        return a;
    }
    else
        return a0;
}
