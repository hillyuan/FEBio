// FEException.h: interface for the FEException class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_FEEXCEPTION_H__A56BADF1_E3BA_4482_AE23_EA40A591ED88__INCLUDED_)
#define AFX_FEEXCEPTION_H__A56BADF1_E3BA_4482_AE23_EA40A591ED88__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <vector>
using namespace std;

class FEElement;

class FEException  
{
public:
	FEException();
	virtual ~FEException();

};

class NegativeJacobian : public FEException
{
public:
	NegativeJacobian(int iel, int ng, double vol, FEElement* pe = 0);

	// print a message to the screen and log file
	void print();

	int		m_iel;	// element where the jacobian was negative
	int		m_ng;	// integration point
	double	m_vol;	// volume
	FEElement*	m_pel;	// pointer to element

public:
	static bool m_boutput;	//!< set to false to suppress output of negative jacobians
};

class ZeroDiagonal : public FEException
{
private:
	struct EQUATION
	{
		int	node;	// node
		int	dof;	// degree of node
	};

public:
	ZeroDiagonal(int node, int dof);

	char m_szerr[256];	// the error message
};

class EnergyDiverging : public FEException {};

class MaxStiffnessReformations : public FEException {};

class ZeroLinestepSize : public FEException {};

class ExitRequest {};

class ForceConversion {};

class IterationFailure {};

class NANDetected {};

class FatalError {};

class FEMultiScaleException
{
public:
	FEMultiScaleException(int eid, int gpt) : elemId(eid), gptIndex(gpt) {}

public:
	int elemId;
	int gptIndex;
};

class DoRunningRestart{};


#endif // !defined(AFX_FEEXCEPTION_H__A56BADF1_E3BA_4482_AE23_EA40A591ED88__INCLUDED_)
