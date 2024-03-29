/**CFile****************************************************************

  FileName    [giaAig.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [Scalable AIG package.]

  Synopsis    [Transformation between AIG manager.]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - June 20, 2005.]

  Revision    [$Id: giaAig.c,v 1.00 2005/06/20 00:00:00 alanmi Exp $]

***********************************************************************/

#include "giaAig.h"
#include "fra.h"
#include "dch.h"
#include "dar.h"

ABC_NAMESPACE_IMPL_START


////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

static inline int Gia_ObjChild0Copy( Aig_Obj_t * pObj )  { return Gia_LitNotCond( Aig_ObjFanin0(pObj)->iData, Aig_ObjFaninC0(pObj) ); }
static inline int Gia_ObjChild1Copy( Aig_Obj_t * pObj )  { return Gia_LitNotCond( Aig_ObjFanin1(pObj)->iData, Aig_ObjFaninC1(pObj) ); }

static inline Aig_Obj_t * Gia_ObjChild0Copy2( Aig_Obj_t ** ppNodes, Gia_Obj_t * pObj, int Id )  { return Aig_NotCond( ppNodes[Gia_ObjFaninId0(pObj, Id)], Gia_ObjFaninC0(pObj) ); }
static inline Aig_Obj_t * Gia_ObjChild1Copy2( Aig_Obj_t ** ppNodes, Gia_Obj_t * pObj, int Id )  { return Aig_NotCond( ppNodes[Gia_ObjFaninId1(pObj, Id)], Gia_ObjFaninC1(pObj) ); }

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    [Derives combinational miter of the two AIGs.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManFromAig_rec( Gia_Man_t * pNew, Aig_Man_t * p, Aig_Obj_t * pObj )
{
    Aig_Obj_t * pNext;
    if ( pObj->iData )
        return;
    assert( Aig_ObjIsNode(pObj) );
    Gia_ManFromAig_rec( pNew, p, Aig_ObjFanin0(pObj) );
    Gia_ManFromAig_rec( pNew, p, Aig_ObjFanin1(pObj) );
    pObj->iData = Gia_ManAppendAnd( pNew, Gia_ObjChild0Copy(pObj), Gia_ObjChild1Copy(pObj) );
    if ( p->pEquivs && (pNext = Aig_ObjEquiv(p, pObj)) )
    {
        int iObjNew, iNextNew;
        Gia_ManFromAig_rec( pNew, p, pNext );
        iObjNew  = Gia_Lit2Var(pObj->iData);
        iNextNew = Gia_Lit2Var(pNext->iData);
        if ( pNew->pNexts )
            pNew->pNexts[iObjNew] = iNextNew;        
    }
}

/**Function*************************************************************

  Synopsis    [Duplicates AIG in the DFS order.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Gia_Man_t * Gia_ManFromAig( Aig_Man_t * p )
{
    Gia_Man_t * pNew;
    Aig_Obj_t * pObj;
    int i;
    // create the new manager
    pNew = Gia_ManStart( Aig_ManObjNum(p) );
    pNew->pName = Gia_UtilStrsav( p->pName );
    pNew->nConstrs = p->nConstrs;
    // create room to store equivalences
    if ( p->pEquivs )
        pNew->pNexts = ABC_CALLOC( int, Aig_ManObjNum(p) );
    // create the PIs
    Aig_ManCleanData( p );
    Aig_ManConst1(p)->iData = 1;
    Aig_ManForEachPi( p, pObj, i )
        pObj->iData = Gia_ManAppendCi( pNew );
    // add logic for the POs
    Aig_ManForEachPo( p, pObj, i )
        Gia_ManFromAig_rec( pNew, p, Aig_ObjFanin0(pObj) );        
    Aig_ManForEachPo( p, pObj, i )
        Gia_ManAppendCo( pNew, Gia_ObjChild0Copy(pObj) );
    Gia_ManSetRegNum( pNew, Aig_ManRegNum(p) );
    if ( pNew->pNexts )
        Gia_ManDeriveReprs( pNew );
    return pNew;
}

/**Function*************************************************************

  Synopsis    [Duplicates AIG in the DFS order.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Gia_Man_t * Gia_ManFromAigSimple( Aig_Man_t * p )
{
    Gia_Man_t * pNew;
    Aig_Obj_t * pObj;
    int i;
    // create the new manager
    pNew = Gia_ManStart( Aig_ManObjNum(p) );
    pNew->pName = Gia_UtilStrsav( p->pName );
    pNew->nConstrs = p->nConstrs;
    // create the PIs
    Aig_ManCleanData( p );
    Aig_ManForEachObj( p, pObj, i )
    {
        if ( Aig_ObjIsAnd(pObj) )
            pObj->iData = Gia_ManAppendAnd( pNew, Gia_ObjChild0Copy(pObj), Gia_ObjChild1Copy(pObj) );
        else if ( Aig_ObjIsPi(pObj) )
            pObj->iData = Gia_ManAppendCi( pNew );
        else if ( Aig_ObjIsPo(pObj) )
            pObj->iData = Gia_ManAppendCo( pNew, Gia_ObjChild0Copy(pObj) );
        else if ( Aig_ObjIsConst1(pObj) )
            pObj->iData = 1;
        else
            assert( 0 );
    }
    Gia_ManSetRegNum( pNew, Aig_ManRegNum(p) );
    return pNew;
}

/**Function*************************************************************

  Synopsis    [Handles choices as additional combinational outputs.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Gia_Man_t * Gia_ManFromAigSwitch( Aig_Man_t * p )
{
    Gia_Man_t * pNew;
    Aig_Obj_t * pObj;
    int i;
    // create the new manager
    pNew = Gia_ManStart( Aig_ManObjNum(p) );
    pNew->pName = Gia_UtilStrsav( p->pName );
    pNew->nConstrs = p->nConstrs;
    // create the PIs
    Aig_ManCleanData( p );
    Aig_ManConst1(p)->iData = 1;
    Aig_ManForEachPi( p, pObj, i )
        pObj->iData = Gia_ManAppendCi( pNew );
    // add POs corresponding to the nodes with choices
    Aig_ManForEachNode( p, pObj, i )
        if ( Aig_ObjRefs(pObj) == 0 )
        {
            Gia_ManFromAig_rec( pNew, p, pObj );        
            Gia_ManAppendCo( pNew, pObj->iData );
        }
    // add logic for the POs
    Aig_ManForEachPo( p, pObj, i )
        Gia_ManFromAig_rec( pNew, p, Aig_ObjFanin0(pObj) );        
    Aig_ManForEachPo( p, pObj, i )
        pObj->iData = Gia_ManAppendCo( pNew, Gia_ObjChild0Copy(pObj) );
    Gia_ManSetRegNum( pNew, Aig_ManRegNum(p) );
    return pNew;
}

/**Function*************************************************************

  Synopsis    [Derives combinational miter of the two AIGs.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManToAig_rec( Aig_Man_t * pNew, Aig_Obj_t ** ppNodes, Gia_Man_t * p, Gia_Obj_t * pObj )
{
    Gia_Obj_t * pNext;
    if ( ppNodes[Gia_ObjId(p, pObj)] )
        return;
    if ( Gia_ObjIsCi(pObj) )
        ppNodes[Gia_ObjId(p, pObj)] = Aig_ObjCreatePi( pNew );
    else
    {
        assert( Gia_ObjIsAnd(pObj) );
        Gia_ManToAig_rec( pNew, ppNodes, p, Gia_ObjFanin0(pObj) );
        Gia_ManToAig_rec( pNew, ppNodes, p, Gia_ObjFanin1(pObj) );
        ppNodes[Gia_ObjId(p, pObj)] = Aig_And( pNew, Gia_ObjChild0Copy2(ppNodes, pObj, Gia_ObjId(p, pObj)), Gia_ObjChild1Copy2(ppNodes, pObj, Gia_ObjId(p, pObj)) );
    }
    if ( pNew->pEquivs && (pNext = Gia_ObjNextObj(p, Gia_ObjId(p, pObj))) )
    {
        Aig_Obj_t * pObjNew, * pNextNew;
        Gia_ManToAig_rec( pNew, ppNodes, p, pNext );
        pObjNew  = ppNodes[Gia_ObjId(p, pObj)];
        pNextNew = ppNodes[Gia_ObjId(p, pNext)];
        if ( pNew->pEquivs )
            pNew->pEquivs[Aig_Regular(pObjNew)->Id] = Aig_Regular(pNextNew);        
    }
}

/**Function*************************************************************

  Synopsis    [Duplicates AIG in the DFS order.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Aig_Man_t * Gia_ManToAig( Gia_Man_t * p, int fChoices )
{
    Aig_Man_t * pNew;
    Aig_Obj_t ** ppNodes;
    Gia_Obj_t * pObj;
    int i;
    assert( !fChoices || (p->pNexts && p->pReprs) );
    // create the new manager
    pNew = Aig_ManStart( Gia_ManAndNum(p) );
    pNew->pName = Gia_UtilStrsav( p->pName );
    pNew->nConstrs = p->nConstrs;
//    pNew->pSpec = Gia_UtilStrsav( p->pName );
    // duplicate representation of choice nodes
    if ( fChoices )
        pNew->pEquivs = ABC_CALLOC( Aig_Obj_t *, Gia_ManObjNum(p) );
    // create the PIs
    ppNodes = ABC_CALLOC( Aig_Obj_t *, Gia_ManObjNum(p) );
    ppNodes[0] = Aig_ManConst0(pNew);
    Gia_ManForEachCi( p, pObj, i )
        ppNodes[Gia_ObjId(p, pObj)] = Aig_ObjCreatePi( pNew );
    // transfer level
    if ( p->vLevels )
    Gia_ManForEachCi( p, pObj, i )
        Aig_ObjSetLevel( ppNodes[Gia_ObjId(p, pObj)], Gia_ObjLevel(p, pObj) );
    // add logic for the POs
    Gia_ManForEachCo( p, pObj, i )
    {
        Gia_ManToAig_rec( pNew, ppNodes, p, Gia_ObjFanin0(pObj) );        
        ppNodes[Gia_ObjId(p, pObj)] = Aig_ObjCreatePo( pNew, Gia_ObjChild0Copy2(ppNodes, pObj, Gia_ObjId(p, pObj)) );
    }
    Aig_ManSetRegNum( pNew, Gia_ManRegNum(p) );
    ABC_FREE( ppNodes );
    return pNew;
}

/**Function*************************************************************

  Synopsis    [Duplicates AIG in the DFS order.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Aig_Man_t * Gia_ManToAigSkip( Gia_Man_t * p, int nOutDelta )
{
    Aig_Man_t * pNew;
    Aig_Obj_t ** ppNodes;
    Gia_Obj_t * pObj;
    int i;
    assert( p->pNexts == NULL && p->pReprs == NULL );
    assert( nOutDelta > 0 && Gia_ManCoNum(p) % nOutDelta == 0 );
    // create the new manager
    pNew = Aig_ManStart( Gia_ManAndNum(p) );
    pNew->pName = Gia_UtilStrsav( p->pName );
    pNew->nConstrs = p->nConstrs;
//    pNew->pSpec = Gia_UtilStrsav( p->pName );
    // create the PIs
    ppNodes = ABC_CALLOC( Aig_Obj_t *, Gia_ManObjNum(p) );
    ppNodes[0] = Aig_ManConst0(pNew);
    Gia_ManForEachCi( p, pObj, i )
        ppNodes[Gia_ObjId(p, pObj)] = Aig_ObjCreatePi( pNew );
    // add logic for the POs
    Gia_ManForEachCo( p, pObj, i )
    {
        Gia_ManToAig_rec( pNew, ppNodes, p, Gia_ObjFanin0(pObj) );        
        if ( i % nOutDelta != 0 )
            continue;
        ppNodes[Gia_ObjId(p, pObj)] = Aig_ObjCreatePo( pNew, Gia_ObjChild0Copy2(ppNodes, pObj, Gia_ObjId(p, pObj)) );
    }
    Aig_ManSetRegNum( pNew, Gia_ManRegNum(p) );
    ABC_FREE( ppNodes );
    return pNew;
}

/**Function*************************************************************

  Synopsis    [Duplicates AIG in the DFS order.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Aig_Man_t * Gia_ManToAigSimple( Gia_Man_t * p )
{
    Aig_Man_t * pNew;
    Aig_Obj_t ** ppNodes;
    Gia_Obj_t * pObj;
    int i;
    ppNodes = ABC_FALLOC( Aig_Obj_t *, Gia_ManObjNum(p) );
    // create the new manager
    pNew = Aig_ManStart( Gia_ManObjNum(p) );
    pNew->pName = Gia_UtilStrsav( p->pName );
    pNew->nConstrs = p->nConstrs;
    // create the PIs
    Gia_ManForEachObj( p, pObj, i )
    {
        if ( Gia_ObjIsAnd(pObj) )
            ppNodes[i] = Aig_And( pNew, Gia_ObjChild0Copy2(ppNodes, pObj, Gia_ObjId(p, pObj)), Gia_ObjChild1Copy2(ppNodes, pObj, Gia_ObjId(p, pObj)) );
        else if ( Gia_ObjIsCi(pObj) )
            ppNodes[i] = Aig_ObjCreatePi( pNew );
        else if ( Gia_ObjIsCo(pObj) )
            ppNodes[i] = Aig_ObjCreatePo( pNew, Gia_ObjChild0Copy2(ppNodes, pObj, Gia_ObjId(p, pObj)) );
        else if ( Gia_ObjIsConst0(pObj) )
            ppNodes[i] = Aig_ManConst0(pNew);
        else
            assert( 0 );
        pObj->Value = Gia_Var2Lit( Aig_ObjId(Aig_Regular(ppNodes[i])), Aig_IsComplement(ppNodes[i]) );
    }
    Aig_ManSetRegNum( pNew, Gia_ManRegNum(p) );
    ABC_FREE( ppNodes );
    return pNew;
}

/**Function*************************************************************

  Synopsis    [Duplicates AIG in the DFS order.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Aig_Man_t * Gia_ManCofactorAig( Aig_Man_t * p, int nFrames, int nCofFanLit )
{
    Aig_Man_t * pMan;
    Gia_Man_t * pGia, * pTemp;
    pGia = Gia_ManFromAig( p );
    pGia = Gia_ManUnrollAndCofactor( pTemp = pGia, nFrames, nCofFanLit, 1 );
    Gia_ManStop( pTemp );
    pMan = Gia_ManToAig( pGia, 0 );
    Gia_ManStop( pGia );
    return pMan;
}


/**Function*************************************************************

  Synopsis    [Transfers representatives from pGia to pAig.]

  Description [Assumes that pGia was created from pAig.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManReprToAigRepr( Aig_Man_t * pAig, Gia_Man_t * pGia )
{
    Aig_Obj_t * pObj;
    Gia_Obj_t * pGiaObj, * pGiaRepr;
    int i;
    assert( pAig->pReprs == NULL );
    assert( pGia->pReprs != NULL );
    // move pointers from AIG to GIA
    Aig_ManForEachObj( pAig, pObj, i )
    {
        assert( i == 0 || !Gia_LitIsCompl(pObj->iData) );
        pGiaObj = Gia_ManObj( pGia, Gia_Lit2Var(pObj->iData) );
        pGiaObj->Value = i;
    }
    // set the pointers to the nodes in AIG
    Aig_ManReprStart( pAig, Aig_ManObjNumMax(pAig) );
    Gia_ManForEachObj( pGia, pGiaObj, i )
    {
        pGiaRepr = Gia_ObjReprObj( pGia, i );
        if ( pGiaRepr == NULL )
            continue;
        Aig_ObjCreateRepr( pAig, Aig_ManObj(pAig, pGiaRepr->Value), Aig_ManObj(pAig, pGiaObj->Value) );
    }
}

/**Function*************************************************************

  Synopsis    [Transfers representatives from pGia to pAig.]

  Description [Assumes that pAig was created from pGia.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManReprToAigRepr2( Aig_Man_t * pAig, Gia_Man_t * pGia )
{
    Gia_Obj_t * pGiaObj, * pGiaRepr;
    int i;
    assert( pAig->pReprs == NULL );
    assert( pGia->pReprs != NULL );
    // set the pointers to the nodes in AIG
    Aig_ManReprStart( pAig, Aig_ManObjNumMax(pAig) );
    Gia_ManForEachObj( pGia, pGiaObj, i )
    {
        pGiaRepr = Gia_ObjReprObj( pGia, i );
        if ( pGiaRepr == NULL )
            continue;
        Aig_ObjCreateRepr( pAig, Aig_ManObj(pAig, Gia_Lit2Var(pGiaRepr->Value)), Aig_ManObj(pAig, Gia_Lit2Var(pGiaObj->Value)) );
    }
}

/**Function*************************************************************

  Synopsis    [Transfers representatives from pAig to pGia.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManReprFromAigRepr( Aig_Man_t * pAig, Gia_Man_t * pGia )
{
    Gia_Obj_t * pObjGia; 
    Aig_Obj_t * pObjAig, * pReprAig;
    int i;
    assert( pAig->pReprs != NULL );
    assert( pGia->pReprs == NULL );
    assert( Gia_ManObjNum(pGia) - Gia_ManCoNum(pGia) == Aig_ManObjNum(pAig) - Aig_ManPoNum(pAig) );
    pGia->pReprs = ABC_CALLOC( Gia_Rpr_t, Gia_ManObjNum(pGia) );
    for ( i = 0; i < Gia_ManObjNum(pGia); i++ )
        Gia_ObjSetRepr( pGia, i, GIA_VOID );
    Gia_ManForEachObj( pGia, pObjGia, i )
    {
//        Abc_Print( 1, "%d -> %d %d\n", i, Gia_ObjValue(pObjGia), Gia_ObjValue(pObjGia)/2 );
        if ( Gia_ObjIsCo(pObjGia) )
            continue;
        assert( i == 0 || !Gia_LitIsCompl(Gia_ObjValue(pObjGia)) );
        pObjAig  = Aig_ManObj( pAig, Gia_Lit2Var(Gia_ObjValue(pObjGia)) );
        pObjAig->iData = i;
    }
    Aig_ManForEachObj( pAig, pObjAig, i )
    {
        if ( Aig_ObjIsPo(pObjAig) )
            continue;
        if ( pAig->pReprs[i] == NULL )
            continue;
        pReprAig = pAig->pReprs[i];
        Gia_ObjSetRepr( pGia, pObjAig->iData, pReprAig->iData );
    }
    pGia->pNexts = Gia_ManDeriveNexts( pGia );
}

/**Function*************************************************************

  Synopsis    [Applies DC2 to the GIA manager.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Gia_Man_t * Gia_ManCompress2( Gia_Man_t * p, int fUpdateLevel, int fVerbose )
{
//    extern Aig_Man_t * Dar_ManCompress2( Aig_Man_t * pAig, int fBalance, int fUpdateLevel, int fFanout, int fPower, int fVerbose );
    Gia_Man_t * pGia;
    Aig_Man_t * pNew, * pTemp;
    pNew = Gia_ManToAig( p, 0 );
    pNew = Dar_ManCompress2( pTemp = pNew, 1, fUpdateLevel, 1, 0, fVerbose );
    Aig_ManStop( pTemp );
    pGia = Gia_ManFromAig( pNew );
    Aig_ManStop( pNew );
    return pGia;
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Gia_Man_t * Gia_ManPerformDch( Gia_Man_t * p, void * pPars )
{
    Gia_Man_t * pGia;
    Aig_Man_t * pNew;
    pNew = Gia_ManToAig( p, 0 );
    pNew = Dar_ManChoiceNew( pNew, (Dch_Pars_t *)pPars );
    pGia = Gia_ManFromAig( pNew );
    Aig_ManStop( pNew );
    return pGia;
}

/**Function*************************************************************

  Synopsis    [Computes equivalences after structural sequential cleanup.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManSeqCleanupClasses( Gia_Man_t * p, int fConst, int fEquiv, int fVerbose )
{
    Aig_Man_t * pNew, * pTemp;
    pNew  = Gia_ManToAigSimple( p );
    pTemp = Aig_ManScl( pNew, fConst, fEquiv, fVerbose );
    Gia_ManReprFromAigRepr( pNew, p );
    Aig_ManStop( pTemp );
    Aig_ManStop( pNew );
}

/**Function*************************************************************

  Synopsis    [Solves SAT problem.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Gia_ManSolveSat( Gia_Man_t * p )
{
//    extern int Fra_FraigSat( Aig_Man_t * pMan, ABC_INT64_T nConfLimit, ABC_INT64_T nInsLimit, int fFlipBits, int fAndOuts, int fVerbose );
    Aig_Man_t * pNew;
    int RetValue, clk = clock();
    pNew = Gia_ManToAig( p, 0 );
    RetValue = Fra_FraigSat( pNew, 10000000, 0, 1, 1, 0 );
    if ( RetValue == 0 )
    {
        Gia_Obj_t * pObj;
        int i, * pInit = (int *)pNew->pData;
        Gia_ManConst0(p)->fMark0 = 0;
        Gia_ManForEachPi( p, pObj, i )
            pObj->fMark0 = pInit[i];
        Gia_ManForEachAnd( p, pObj, i )
            pObj->fMark0 = (Gia_ObjFanin0(pObj)->fMark0 ^ Gia_ObjFaninC0(pObj)) & 
                           (Gia_ObjFanin1(pObj)->fMark0 ^ Gia_ObjFaninC1(pObj));
        Gia_ManForEachPo( p, pObj, i )
            pObj->fMark0 = (Gia_ObjFanin0(pObj)->fMark0 ^ Gia_ObjFaninC0(pObj));
        Gia_ManForEachPo( p, pObj, i )
            if ( pObj->fMark0 != 1 )
                break;
        if ( i != Gia_ManPoNum(p) )
            Abc_Print( 1, "Counter-example verification has failed.  " );
//        else
//            Abc_Print( 1, "Counter-example verification succeeded.  " );
    }
/*
    else if ( RetValue == 1 )
        Abc_Print( 1, "The SAT problem is unsatisfiable.  " );
    else if ( RetValue == -1 )
        Abc_Print( 1, "The SAT problem is undecided.  " );
    Abc_PrintTime( 1, "Time", clock() - clk );
*/
    Aig_ManStop( pNew );
    return RetValue;
}


////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_IMPL_END

