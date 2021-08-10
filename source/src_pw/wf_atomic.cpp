#include "wf_atomic.h"
#include "global.h"
#include "../module_base/math_integral.h"
#include "../module_base/math_sphbes.h"
#include "../module_base/math_polyint.h"
#include "../module_base/math_ylmreal.h"
#include "soc.h"
#include <complex>

WF_atomic::WF_atomic()
{
    evc  = new ComplexMatrix[1];
    wanf2= new ComplexMatrix[1];
}

WF_atomic::~WF_atomic()
{
	if(GlobalV::test_deconstructor)
	{
		std::cout << " ~WF_atomic()" << std::endl;
	}
    delete[] evc;
    delete[] wanf2;
}

//==========================================================
// MEMBER FUNCTION :
// NAME : init_at_1(init a table with the radial Fourier
// transform of the atomic WF_atomictions)
//==========================================================
void WF_atomic::init_at_1(void)
{
    if (GlobalV::test_wf) TITLE("WF_atomic","init_at_1");
    timer::tick("WF_atomic","init_at_1");

	GlobalV::ofs_running << "\n Make real space PAO into reciprocal space." << std::endl;

    this->print_PAOs();

//----------------------------------------------------------
// EXPLAIN : Find the type of atom that has most mesh points.
//----------------------------------------------------------
    int ndm = 0;
    for (int it=0; it<GlobalC::ucell.ntype; it++)
    {
        ndm = (GlobalC::ucell.atoms[it].msh > ndm) ? GlobalC::ucell.atoms[it].msh : ndm;
    }
	OUT(GlobalV::ofs_running,"max mesh points in Pseudopotential",ndm);

    // needed to normalize atomic wfcs (not a bad idea in general and
    // necessary to compute correctly lda+U projections)
    GlobalC::ppcell.tab_at.zero_out();
//----------------------------------------------------------
// EXPLAIN : If use gauss orbitals to represent aotmic
// orbitals (controlled by parameters)
//
// USE GLOBAL CLASS VARIABLES :
// NAME : GlobalC::ucell.atoms.nchi
// NAME : GlobalC::ucell.atoms.msh(number of mesh points)
// NAME : GlobalC::ucell.atoms.r
//----------------------------------------------------------
    const int startq = 0;
    const double pref = FOUR_PI / sqrt(GlobalC::ucell.omega);
    double *aux = new double[ndm];
    double *vchi = new double[ndm];

	OUT(GlobalV::ofs_running,"dq(describe PAO in reciprocal space)",GlobalV::DQ);
	OUT(GlobalV::ofs_running,"max q",GlobalV::NQX);

    for (int it=0; it<GlobalC::ucell.ntype; it++)
    {
		Atom* atom = &GlobalC::ucell.atoms[it];

		GlobalV::ofs_running <<"\n number of pseudo atomic orbitals for "
		<< atom->label << " is " << atom->nchi << std::endl;

        for (int ic=0; ic<atom->nchi ;ic++)
        {
			//std::cout << "\n T=" << it << " ic=" << ic << std::endl;
            int nmesh;
            if(GlobalV::RENORMWITHMESH)
                nmesh = atom->mesh;
            else
                nmesh = atom->msh;

            // check the unit condition
            double *inner_part = new double[nmesh];
            for (int ir=0; ir<nmesh; ir++)
            {
                inner_part[ir] = atom->chi(ic,ir) * atom->chi(ic,ir);
            }
            double unit = 0.0;
            Integral::Simpson_Integral(nmesh, inner_part, atom->rab, unit);
            delete[] inner_part;

			GlobalV::ofs_running << " the unit of pseudo atomic orbital is " << unit;

            //=================================
            // normalize radial wave functions
            //=================================
            for (int ir=0; ir<nmesh; ir++)
            {
                atom->chi(ic,ir) /= sqrt(unit);
            }

            //===========
            // recheck
            //===========
            inner_part = new double[nmesh];
            for (int ir=0; ir<nmesh; ir++)
            {
                inner_part[ir] = atom->chi(ic,ir) * atom->chi(ic,ir);
            }
            unit = 0.0;
            Integral::Simpson_Integral(nmesh, inner_part, atom->rab, unit);
            delete[] inner_part;

			GlobalV::ofs_running << ", renormalize to " << unit << std::endl;

            if (atom->oc[ic] >= 0.0)
            {
                const int l = atom->lchi[ic];
                for (int iq=startq; iq<GlobalV::NQX; iq++)
                {
                    const double q = GlobalV::DQ * iq;
                    Sphbes::Spherical_Bessel(atom->msh, atom->r, q, l, aux);
                    for (int ir = 0;ir < atom->msh;ir++)
                    {
                        vchi[ir] = atom->chi(ic,ir) * aux[ir] * atom->r[ir];
                    }

                    double vqint = 0.0;
                    Integral::Simpson_Integral(atom->msh, vchi, atom->rab, vqint);

                    GlobalC::ppcell.tab_at(it, ic, iq) =  vqint * pref;
                    //				if( it == 0 && ic == 0 )
                    //				{
                    //
                    //					for (ir = 0;ir < GlobalC::ucell.atoms[it].msh;ir++)
                    //						GlobalV::ofs_running << std::setprecision(20) << "\n vchi(" << ir << ")=" << vchi[ir];
                    //					GlobalV::ofs_running<<"\n aux[0] = "<<aux[0];
                    //					GlobalV::ofs_running<<"\n msh = "<< GlobalC::ucell.atoms[it].msh;
                    //					GlobalV::ofs_running<<"\n tab_at : "<<ppcell.tab_at(it, ic, iq) ;
                    //					GlobalV::ofs_running<<"\n pref = "<<pref;
                    //				}
                } // enddo
            } // endif
        } // enddo
    } // enddo

    delete [] aux;
    delete [] vchi;
    timer::tick("WF_atomic","init_at_1");
    return;
}// end init_at_1

void WF_atomic::print_PAOs(void)const
{
    if (GlobalV::MY_RANK!=0) return;
    for (int it=0; it<GlobalC::ucell.ntype; it++)
    {
        for (int icc=0; icc<GlobalC::ucell.atoms[it].nchi ;icc++)
        {
            int ic = icc;
            if(GlobalV::NSPIN==4) ic = icc/2;
            std::string orbital_type;
            if (ic == 0)  orbital_type = "S";
            else if (ic == 1) orbital_type = "P";
            else if (ic == 2) orbital_type = "D";
			else if (ic == 3) orbital_type = "F"; // mohan add 2009-12-15
			else if (ic == 4) orbital_type = "G"; // liuyu add 2021-05-07
            else
            {
				GlobalV::ofs_warning << "\n nchi = " << GlobalC::ucell.atoms[it].nchi << std::endl;
                WARNING_QUIT("WF_atomic::print_PAOs", "unknown PAO type.");
            }

            std::stringstream ss;
            ss << GlobalV::global_out_dir
            << GlobalC::ucell.atoms[it].label << "/"
            << GlobalC::ucell.atoms[it].label << "-"
            << orbital_type << ".ORBITAL";

            std::ofstream ofs(ss.str().c_str());
            ofs << "Mesh " << GlobalC::ucell.atoms[it].msh;
            ofs << "\n" << std::setw(15) << "Radial"
            << std::setw(15) << "Psi"
            << std::setw(15) << "Rab";

            for (int i=0;i<GlobalC::ucell.atoms[it].msh;i++)
            {
                ofs << "\n" << std::setw(15) << GlobalC::ucell.atoms[it].r[i]
                << std::setw(15) << GlobalC::ucell.atoms[it].chi(icc,i)
                << std::setw(15) << GlobalC::ucell.atoms[it].rab[i];
            }
            ofs.close();
        }
        // end out put
    }
    return;
}


//===================================================================
// This routine computes an estimate of the start_ WF_atomictions
// from superposition of atomic WF_atomictions or random wave functions.
//===================================================================
// from wfcinit.f90

void WF_atomic::check_psi(const ComplexMatrix *evc)const
{
    std::cout<<"\n Check psi : \n";

    for (int iw=0;iw<GlobalV::NBANDS;iw++)
    {
        double sum_evc = abs2_row(this->evc[0],iw);
        OUT("iw",iw);
        OUT("sum_evc",sum_evc);
    }

    for (int ik=0;ik<GlobalC::kv.nks;ik++)
    {
        double sum_evc = abs2(this->evc[ik]);
        OUT("ik",ik);
        OUT("sum_evc2",sum_evc);
    }
    //QUIT();
}

void WF_atomic::atomic_wfc
(	const int ik,
  const int np,
  const int lmax_wfc,
  ComplexMatrix &wfcatom,
  const realArray &table_q,
  const int &table_dimension,
  const double &dq
)const
{
    if (GlobalV::test_wf>3) TITLE("WF_atomic","atomic_wfc");
    timer::tick("WF_atomic","atomic_wfc");
    //=========================================================
    // This routine computes the superposition of atomic
    // WF_atomictions for a given k-point.
    //=========================================================
    const int total_lm = (lmax_wfc + 1) * (lmax_wfc + 1);
    matrix ylm(total_lm, np);
    std::complex<double> *aux = new std::complex<double>[np];
    double *chiaux = new double[1];

    Vector3<double> *gk = new Vector3 <double> [np];
    for (int ig=0;ig<np;ig++)
    {
        gk[ig] = WF_atomic::get_1qvec_cartesian(ik, ig);
    }
    //ylm = spherical harmonics functions
    YlmReal::Ylm_Real(total_lm, np, gk, ylm);
    int index = 0;
    //---------------------------------------------------------
    // Calculate G space 3D wave functions
    //---------------------------------------------------------
    double *flq = new double[np];
    for (int it = 0;it < GlobalC::ucell.ntype;it++)
    {
        for (int ia = 0;ia < GlobalC::ucell.atoms[it].na;ia++)
        {
			//std::cout << "\n it = " << it << " ia = " << ia << std::endl;
            std::complex<double> *sk = this->get_sk(ik, it, ia);
            //-------------------------------------------------------
            // Calculate G space 3D wave functions
            //-------------------------------------------------------
            for (int iw = 0;iw < GlobalC::ucell.atoms[it].nchi;iw++)
            {
                if (GlobalC::ucell.atoms[it].oc[iw] >= 0.0)
                {
                    const int l = GlobalC::ucell.atoms[it].lchi[iw];
                    std::complex<double> lphase = pow(NEG_IMAG_UNIT, l);
                    //-----------------------------------------------------
                    //  the factor i^l MUST BE PRESENT in order to produce
                    //  WF_atomictions for k=0 that are real in real space
                    //-----------------------------------------------------

                      //---------------------------------------------------------
                      // flq = radial fourier transform of atomic orbitals chi
                      //---------------------------------------------------------
                    for (int ig=0; ig<np; ig++)
                    {
                        flq[ig] =
                            PolyInt::Polynomial_Interpolation(table_q,
                                                               it, iw, table_dimension, dq, gk[ig].norm() * GlobalC::ucell.tpiba );
                    }

                    if(GlobalV::NSPIN==4)
                    {
                        if(GlobalC::ucell.atoms[it].has_so)
                        {
                            Soc soc;
						    soc.rot_ylm(l+1);
                            const double j = GlobalC::ucell.atoms[it].jchi[iw];
                            if ( !(GlobalV::DOMAG||GlobalV::DOMAG_Z))
                            {//atomic_wfc_so
                                double fact[2];
                                for(int m=-l-1;m<l+1;m++)
                                {
                                   fact[0] = soc.spinor(l,j,m,0);
                                   fact[1] = soc.spinor(l,j,m,1);
                                   if (fabs(fact[0])>1e-8||fabs(fact[1])>1e-8)
                                   {
                                      for(int is=0;is<2;is++)
                                      {
                                          if(fabs(fact[is])>1e-8)
                                          {
                                              const int ind = GlobalC::ppcell.lmaxkb + soc.sph_ind(l,j,m,is);
                                              ZEROS(aux, np);
                                              for(int n1=0;n1<2*l+1;n1++){
                                                 const int lm = l*l +n1;
                                                 if(abs(soc.rotylm(n1,ind))>1e-8)
                                                   for(int ig=0; ig<np;ig++)
                                                      aux[ig] += soc.rotylm(n1,ind)* ylm(lm,ig);
                                              }
                                              for(int ig=0; ig<np;ig++)
                                                 wfcatom(index, ig + this->npwx*is ) = lphase * fact[is] * sk[ig] * aux[ig] * flq[ig];
                                          }
                                          else
                                            for(int ig=0; ig<np;ig++) wfcatom(index,ig+ this->npwx*is) = std::complex<double>(0.0 , 0.0);
                                      }//is
                                      index++;
                                   }//if
                                }//m
                            }//if
                            else
                            {//atomic_wfc_so_mag

                              double alpha, gamma;
                              std::complex<double> fup,fdown;
                              int nc;
                              //This routine creates two functions only in the case j=l+1/2 or exit in the other case
                              if(fabs(j-l+0.5<1e-4)) continue;
                              delete[] chiaux;
                              chiaux = new double [np];
                              //Find the functions j= l- 1/2
                              if(l==0)
                                 for(int ig=0;ig<np;ig++){
                                    chiaux[ig] = flq[ig];
                                 }
                              else
                              {
                                 for(int ib = 0;ib < GlobalC::ucell.atoms[it].nchi;ib++)
                                 {
                                    if((GlobalC::ucell.atoms[it].lchi[ib] == l)&&(fabs(GlobalC::ucell.atoms[it].jchi[ib]-l+0.5)<1e-4))
                                    {
                                       nc=ib;
                                       break;
                                    }
                                 }
                                 for(int ig=0;ig<np;ig++)
                                 {//Average the two functions
                                    chiaux[ig] =  l *
                                         PolyInt::Polynomial_Interpolation(table_q,
                                                               it, nc, table_dimension, dq, gk[ig].norm() * GlobalC::ucell.tpiba );

                                    chiaux[ig] += flq[ig] * (l+1.0) ;
                                    chiaux[ig] *= 1/(2.0*l+1.0);
                                 }
                              }
                              //and construct the starting wavefunctions as in the noncollinear case.
                              alpha = GlobalC::ucell.magnet.angle1_[it];
                              gamma = -1 * GlobalC::ucell.magnet.angle2_[it] + 0.5 * PI;

                              for(int m = 0;m<2*l+1;m++)
                              {
                                 const int lm = l*l +m;
                                 if(index+2*l+1>GlobalC::ucell.natomwfc) WARNING_QUIT("GlobalC::wf.atomic_wfc()","error: too many wfcs");
                                 for(int ig = 0;ig<np;ig++)
                                 {
                                     aux[ig] = sk[ig] * ylm(lm,ig) * chiaux[ig];
                                 }
                                 //rotate wfc as needed
                                 //first rotation with angle alpha around (OX)
                                 for(int ig = 0;ig<np;ig++)
                                 {
                                     fup = cos(0.5 * alpha) * aux[ig];
                                     fdown = IMAG_UNIT * sin(0.5* alpha) * aux[ig];
                                     //build the orthogonal wfc
                                     //first rotation with angle (alpha + PI) around (OX)
                                     wfcatom(index,ig) = (cos(0.5 * gamma) + IMAG_UNIT * sin(0.5*gamma)) * fup;
                                     wfcatom(index,ig+ this->npwx) = (cos(0.5 * gamma) - IMAG_UNIT * sin(0.5*gamma)) * fdown;
                                     //second rotation with angle gamma around(OZ)
                                     fup = cos(0.5 * (alpha + PI))*aux[ig];
                                     fdown = IMAG_UNIT * sin(0.5 * (alpha + PI))*aux[ig];
                                     wfcatom(index+2*l+1,ig) = (cos(0.5*gamma) + IMAG_UNIT*sin(0.5*gamma))*fup;
                                     wfcatom(index+2*l+1,ig+ this->npwx) = (cos(0.5*gamma) - IMAG_UNIT*sin(0.5*gamma))*fdown;
                                 }
                                 index++;
                              }
                              index += 2*l +1;
                            }
                        }
                        else
                        {//atomic_wfc_nc
                            double alpha, gamman;
                            std::complex<double> fup, fdown;
                            alpha = GlobalC::ucell.magnet.angle1_[it];
                            gamman = -GlobalC::ucell.magnet.angle2_[it] + 0.5*PI;
                            for(int m = 0;m<2*l+1;m++)
                            {
                                const int lm = l*l +m;
                                if(index+2*l+1>GlobalC::ucell.natomwfc) WARNING_QUIT("GlobalC::wf.atomic_wfc()","error: too many wfcs");
                                for(int ig = 0;ig<np;ig++)
                                {
                                     aux[ig] = sk[ig] * ylm(lm,ig) * flq[ig];
                                }
                                //rotate function
                                //first, rotation with angle alpha around(OX)
                                for(int ig = 0;ig<np;ig++)
                                {
                                     fup = cos(0.5*alpha) * aux[ig];
                                     fdown = IMAG_UNIT * sin(0.5* alpha) * aux[ig];
                                     //build the orthogonal wfc
                                     //first rotation with angle(alpha+PI) around(OX)
                                     wfcatom(index,ig) = (cos(0.5 * gamman) + IMAG_UNIT * sin(0.5*gamman)) * fup;
                                     wfcatom(index,ig+ this->npwx) = (cos(0.5 * gamman) - IMAG_UNIT * sin(0.5*gamman)) * fdown;
                                     //second rotation with angle gamma around(OZ)
                                     fup = cos(0.5 * (alpha + PI)) * aux[ig];
                                     fdown = IMAG_UNIT * sin(0.5 * (alpha + PI)) * aux[ig];
                                     wfcatom(index+2*l+1,ig) = (cos(0.5*gamman) + IMAG_UNIT*sin(0.5*gamman))*fup;
                                     wfcatom(index+2*l+1,ig+ this->npwx) = (cos(0.5*gamman) - IMAG_UNIT*sin(0.5*gamman))*fdown;
                                }
                                index++;
                            }
                            index += 2*l+1;
                        }
                       // index++;
                    }
                    else{//LSDA and nomagnet case

                      for (int m=0; m<2*l+1; m++)
                      {
                        const int lm = l * l + m;
                        for (int ig=0;ig<np;ig++)
                        {
                            wfcatom(index, ig) = lphase * sk [ig] * ylm(lm, ig) * flq[ig];
                            // very useful for debug, don't delete this
//							if(i==24 && index==0){
//							std::cout << "\n wfcatom(" << index<<","<<i<<") = " << wfcatom(index,i);
//							std::cout << "\n ylm(" << lm <<","<<i<<") = " << ylm (lm,i)
//							    << " sk[i]=" <<sk[i] << " chiq=" << chiq (it,iw,i) ;
//							}
                        }
                        index++;
                      }//end m
                    }
                } //end if
            } //end iw
			delete [] sk;
        } //end ia //mohan modify 2007-11-7
    } // end nt

	if(GlobalV::test_wf)OUT(GlobalV::ofs_running,"wf_index",index);

    if (index != GlobalC::ucell.natomwfc)
    {
        WARNING_QUIT("GlobalC::wf.atomic_wfc()","index != GlobalC::ucell.natomwfc");
    }
    delete[] flq;
    delete [] gk;
    delete [] aux;
    delete[] chiaux;
    timer::tick("WF_atomic","atomic_wfc");
    return;
} //end subroutine atomic_wfc

void WF_atomic::random(ComplexMatrix &psi,const int iw_start,const int iw_end,const int ik)const
{
    assert(iw_start >= 0);
    assert(psi.nr >= iw_end);
    const int ng = GlobalC::kv.ngk[ik];
    for (int iw = iw_start ;iw < iw_end;iw++)
    {
        for (int ig = 0;ig < ng;ig++)
        {
            const double rr = std::rand()/double(RAND_MAX); //qianrui add RAND_MAX
            const double arg= TWO_PI * std::rand()/double(RAND_MAX);
            Vector3<double> v3 = GlobalC::pw.get_GPlusK_cartesian(ik, this->igk(ik, ig));
            psi(iw,ig) = std::complex<double>(rr * cos(arg), rr * sin(arg)) / (v3 * v3 + 1.0);
        }
        if(GlobalV::NPOL==2)for (int ig = this->npwx;ig < this->npwx + ng;ig++)
        {
            const double rr = std::rand()/double(RAND_MAX);
            const double arg= TWO_PI * std::rand()/double(RAND_MAX);
            Vector3<double> v3 = GlobalC::pw.get_GPlusK_cartesian(ik, this->igk(ik, ig - this->npwx));
            psi(iw,ig) = std::complex<double>(rr * cos(arg), rr * sin(arg)) / (v3 * v3 + 1.0);
        }
    }
    return;
}
