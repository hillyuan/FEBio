#pragma once
#include "FECore/FEContactInterface.h"
#include "FEContactSurface.h"

//-----------------------------------------------------------------------------
class FESlidingSurface3 : public FEContactSurface
{
public:
	//! constructor
	FESlidingSurface3(FEModel* pfem);
	
	//! destructor
	~FESlidingSurface3() {}
	
	//! initialization
	bool Init();
	
	//! shallow copy
	void ShallowCopy(FESlidingSurface3& s);
	
	//! calculate the nodal normals
	void UpdateNodeNormals();
	
	void Serialize(DumpFile& ar);
	
	void SetPoroMode(bool bporo) { m_bporo = bporo; }
	
protected:
	FEModel*	m_pfem;
	
public:
	bool						m_bporo;	//!< set poro-mode
	bool						m_bsolu;	//!< set solute-mode
	
	vector<double>				m_gap;	//!< gap function at integration points
	vector<vec3d>				m_nu;	//!< normal at integration points
	vector<vec2d>				m_rs;	//!< natural coordinates of projection of integration point
	vector<double>				m_Lmd;	//!< lagrange multipliers for displacements
	vector<double>				m_Lmp;	//!< lagrange multipliers for fluid pressures
	vector<double>				m_Lmc;	//!< lagrange multipliers for solute concentrations
	vector<FESurfaceElement*>	m_pme;	//!< master element of projected integration point
	vector<double>				m_Ln;	//!< net contact pressure
	vector<int>					m_nei;	//!< surface element indices into arrays
	vector<bool>				m_poro;	//!< surface element poro status
	vector<int>					m_solu;	//!< surface element solute id
	
	vector<double>	m_epsn;	//!< penalty factors
	vector<double>	m_epsp;	//!< pressure penalty factors
	vector<double>	m_epsc;	//!< concentration penalty factors
	
	vector<vec3d>		m_nn;	//!< node normals
	
	// biphasic-solute data
	vector<double>				m_pg;	//!< pressure "gap"
	vector<double>				m_cg;	//!< concentration "gap"
};

//-----------------------------------------------------------------------------
class FESlidingInterface3 :	public FEContactInterface
{
public:
	//! constructor
	FESlidingInterface3(FEModel* pfem);
	
	//! destructor
	~FESlidingInterface3();
	
	//! initialization
	bool Init();
	
	//! update
	void Update(int niter);
	
	//! Create a shallow copy
	void ShallowCopy(FEContactInterface& ci);
	
	//! calculate contact forces
	void ContactForces(vector<double>& F, FENLSolver* psolver);
	
	//! calculate contact stiffness
	void ContactStiffness(FENLSolver* psolver);

	//! calculate contact pressures for file output
	void UpdateContactPressures();

	//! calculate Lagrangian augmentations
	bool Augment(int naug);
	
	//! serialize data to archive
	void Serialize(DumpFile& ar);
	
	//! mark ambient condition 
	void MarkAmbient();
	
	//! set ambient condition 
	void SetAmbient();

	//! return the master and slave surface
	FESurface* GetMasterSurface() { return &m_ms; }
	FESurface* GetSlaveSurface () { return &m_ss; }

protected:
	void ProjectSurface(FESlidingSurface3& ss, FESlidingSurface3& ms, bool bupseg);
	
	//! calculate penalty factor
	void CalcAutoPenalty(FESlidingSurface3& s);
	
	void CalcAutoPressurePenalty(FESlidingSurface3& s);
	double AutoPressurePenalty(FESurfaceElement& el, FESlidingSurface3& s);
	
	void CalcAutoConcentrationPenalty(FESlidingSurface3& s);
	double AutoConcentrationPenalty(FESurfaceElement& el, FESlidingSurface3& s);

public:
	FESlidingSurface3	m_ms;	//!< master surface
	FESlidingSurface3	m_ss;	//!< slave surface
	
	int				m_knmult;		//!< higher order stiffness multiplier
	bool			m_btwo_pass;	//!< two-pass flag
	double			m_atol;			//!< augmentation tolerance
	double			m_gtol;			//!< gap tolerance
	double			m_ptol;			//!< pressure gap tolerance
	double			m_ctol;			//!< concentration gap tolerance
	double			m_stol;			//!< search tolerance
	bool			m_bsymm;		//!< use symmetric stiffness components only
	double			m_srad;			//!< contact search radius
	int				m_naugmax;		//!< maximum nr of augmentations
	int				m_naugmin;		//!< minimum nr of augmentations
	int				m_nsegup;		//!< segment update parameter
	
	double			m_epsn;		//!< normal penalty factor
	bool			m_bautopen;	//!< use autopenalty factor
	
	// biphasic-solute contact parameters
	double	m_epsp;		//!< fluid volumetric flow rate penalty
	double	m_epsc;		//!< solute molar flow rate penalty
	double	m_Rgas;		//!< universal gas constant
	double	m_Tabs;		//!< absolute temperature
	double	m_ambp;		//!< ambient pressure
	double	m_ambc;		//!< ambient concentration

	DECLARE_PARAMETER_LIST();
};
