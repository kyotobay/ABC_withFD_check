/**CFile****************************************************************

  FileName    [giaDfs.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [Scalable AIG package.]

  Synopsis    [DFS procedures.]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - June 20, 2005.]

  Revision    [$Id: giaDfs.c,v 1.00 2005/06/20 00:00:00 alanmi Exp $]

***********************************************************************/

#include "gia.h"

ABC_NAMESPACE_IMPL_START


////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    [Counts the support size of the node.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManCollectCis_rec( Gia_Man_t * p, Gia_Obj_t * pObj, Vec_Int_t * vSupp )
{
    if ( Gia_ObjIsTravIdCurrent(p, pObj) )
        return;
    Gia_ObjSetTravIdCurrent(p, pObj);
    if ( Gia_ObjIsCi(pObj) )
    {
        Vec_IntPush( vSupp, Gia_ObjId(p, pObj) );
        return;
    }
    assert( Gia_ObjIsAnd(pObj) );
    Gia_ManCollectCis_rec( p, Gia_ObjFanin0(pObj), vSupp );
    Gia_ManCollectCis_rec( p, Gia_ObjFanin1(pObj), vSupp );
}

/**Function*************************************************************

  Synopsis    [Collects support nodes.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManCollectCis( Gia_Man_t * p, int * pNodes, int nNodes, Vec_Int_t * vSupp )
{
    Gia_Obj_t * pObj;
    int i; 
    Vec_IntClear( vSupp );
    Gia_ManIncrementTravId( p );
    Gia_ObjSetTravIdCurrent( p, Gia_ManConst0(p) );
    for ( i = 0; i < nNodes; i++ )
    {
        pObj = Gia_ManObj( p, pNodes[i] );
        if ( Gia_ObjIsCo(pObj) )
            Gia_ManCollectCis_rec( p, Gia_ObjFanin0(pObj), vSupp );
        else
            Gia_ManCollectCis_rec( p, pObj, vSupp );
    }
}

/**Function*************************************************************

  Synopsis    [Counts the support size of the node.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManCollectAnds_rec( Gia_Man_t * p, Gia_Obj_t * pObj, Vec_Int_t * vNodes )
{
    if ( Gia_ObjIsTravIdCurrent(p, pObj) )
        return;
    Gia_ObjSetTravIdCurrent(p, pObj);
    if ( Gia_ObjIsCi(pObj) )
        return;
    assert( Gia_ObjIsAnd(pObj) );
    Gia_ManCollectAnds_rec( p, Gia_ObjFanin0(pObj), vNodes );
    Gia_ManCollectAnds_rec( p, Gia_ObjFanin1(pObj), vNodes );
    Vec_IntPush( vNodes, Gia_ObjId(p, pObj) );
}

/**Function*************************************************************

  Synopsis    [Collects support nodes.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManCollectAnds( Gia_Man_t * p, int * pNodes, int nNodes, Vec_Int_t * vNodes )
{
    Gia_Obj_t * pObj;
    int i; 
    Vec_IntClear( vNodes );
//    Gia_ManIncrementTravId( p );
    Gia_ObjSetTravIdCurrent( p, Gia_ManConst0(p) );
    for ( i = 0; i < nNodes; i++ )
    {
        pObj = Gia_ManObj( p, pNodes[i] );
        if ( Gia_ObjIsCo(pObj) )
            Gia_ManCollectAnds_rec( p, Gia_ObjFanin0(pObj), vNodes );
        else
            Gia_ManCollectAnds_rec( p, pObj, vNodes );
    }
}

/**Function*************************************************************

  Synopsis    [Counts the support size of the node.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManCollectNodesCis_rec( Gia_Man_t * p, Gia_Obj_t * pObj, Vec_Int_t * vNodes )
{
    if ( Gia_ObjIsTravIdCurrent(p, pObj) )
        return;
    Gia_ObjSetTravIdCurrent(p, pObj);
    if ( Gia_ObjIsCi(pObj) )
    {
        Vec_IntPush( vNodes, Gia_ObjId(p, pObj) );
        return;
    }
    assert( Gia_ObjIsAnd(pObj) );
    Gia_ManCollectNodesCis_rec( p, Gia_ObjFanin0(pObj), vNodes );
    Gia_ManCollectNodesCis_rec( p, Gia_ObjFanin1(pObj), vNodes );
    Vec_IntPush( vNodes, Gia_ObjId(p, pObj) );
}

/**Function*************************************************************

  Synopsis    [Collects support nodes.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Vec_Int_t * Gia_ManCollectNodesCis( Gia_Man_t * p, int * pNodes, int nNodes )
{
    Vec_Int_t * vNodes;
    Gia_Obj_t * pObj;
    int i; 
    vNodes = Vec_IntAlloc( 10000 );
    Gia_ManIncrementTravId( p );
    Gia_ObjSetTravIdCurrent( p, Gia_ManConst0(p) );
    for ( i = 0; i < nNodes; i++ )
    {
        pObj = Gia_ManObj( p, pNodes[i] );
        if ( Gia_ObjIsCo(pObj) )
            Gia_ManCollectNodesCis_rec( p, Gia_ObjFanin0(pObj), vNodes );
        else
            Gia_ManCollectNodesCis_rec( p, pObj, vNodes );
    }
    return vNodes;
}

/**Function*************************************************************

  Synopsis    [Collects support nodes.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManCollectTest( Gia_Man_t * p )
{
    Vec_Int_t * vNodes;
    Gia_Obj_t * pObj;
    int i, iNode, clk = clock();
    vNodes = Vec_IntAlloc( 100 );
    Gia_ManIncrementTravId( p );
    Gia_ManForEachCo( p, pObj, i )
    {
        iNode = Gia_ObjId(p, pObj);
        Gia_ManCollectAnds( p, &iNode, 1, vNodes );
    }
    Vec_IntFree( vNodes );
    ABC_PRT( "DFS from each output", clock() - clk );
}

/**Function*************************************************************

  Synopsis    [Collects support nodes.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Gia_ManSuppSize_rec( Gia_Man_t * p, Gia_Obj_t * pObj )
{
    if ( Gia_ObjIsTravIdCurrent(p, pObj) )
        return 0;
    Gia_ObjSetTravIdCurrent(p, pObj);
    if ( Gia_ObjIsCi(pObj) )
        return 1;
    assert( Gia_ObjIsAnd(pObj) );
    return Gia_ManSuppSize_rec( p, Gia_ObjFanin0(pObj) ) +
        Gia_ManSuppSize_rec( p, Gia_ObjFanin1(pObj) );
}

/**Function*************************************************************

  Synopsis    [Collects support nodes.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Gia_ManSuppSize( Gia_Man_t * p, int * pNodes, int nNodes )
{
    Gia_Obj_t * pObj;
    int i, Counter = 0; 
    Gia_ManIncrementTravId( p );
    Gia_ObjSetTravIdCurrent( p, Gia_ManConst0(p) );
    for ( i = 0; i < nNodes; i++ )
    {
        pObj = Gia_ManObj( p, pNodes[i] );
        if ( Gia_ObjIsCo(pObj) )
            Counter += Gia_ManSuppSize_rec( p, Gia_ObjFanin0(pObj) );
        else
            Counter += Gia_ManSuppSize_rec( p, pObj );
    }
    return Counter;
}

/**Function*************************************************************

  Synopsis    [Collects support nodes.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Gia_ManConeSize_rec( Gia_Man_t * p, Gia_Obj_t * pObj )
{
    if ( Gia_ObjIsTravIdCurrent(p, pObj) )
        return 0;
    Gia_ObjSetTravIdCurrent(p, pObj);
    if ( Gia_ObjIsCi(pObj) )
        return 0;
    assert( Gia_ObjIsAnd(pObj) );
    return 1 + Gia_ManConeSize_rec( p, Gia_ObjFanin0(pObj) ) +
        Gia_ManConeSize_rec( p, Gia_ObjFanin1(pObj) );
}

/**Function*************************************************************

  Synopsis    [Collects support nodes.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Gia_ManConeSize( Gia_Man_t * p, int * pNodes, int nNodes )
{
    Gia_Obj_t * pObj;
    int i, Counter = 0; 
    Gia_ManIncrementTravId( p );
    Gia_ObjSetTravIdCurrent( p, Gia_ManConst0(p) );
    for ( i = 0; i < nNodes; i++ )
    {
        pObj = Gia_ManObj( p, pNodes[i] );
        if ( Gia_ObjIsCo(pObj) )
            Counter += Gia_ManConeSize_rec( p, Gia_ObjFanin0(pObj) );
        else
            Counter += Gia_ManConeSize_rec( p, pObj );
    }
    return Counter;
}

/**Function*************************************************************

  Synopsis    [Levelizes the nodes.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Vec_Vec_t * Gia_ManLevelize( Gia_Man_t * p )
{ 
    Gia_Obj_t * pObj;
    Vec_Vec_t * vLevels;
    int nLevels, Level, i;
    nLevels = Gia_ManLevelNum( p );
    vLevels = Vec_VecStart( nLevels + 1 );
    Gia_ManForEachAnd( p, pObj, i )
    {
        Level = Gia_ObjLevel( p, pObj );
        assert( Level <= nLevels );
        Vec_VecPush( vLevels, Level, pObj );
    }
    return vLevels;
}

/**Function*************************************************************

  Synopsis    [Computes reverse topological order.]

  Description [Assumes that levels are already assigned.
  The levels of CO nodes may not be assigned.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Vec_Int_t * Gia_ManOrderReverse( Gia_Man_t * p )
{
    Gia_Obj_t * pObj;
    Vec_Vec_t * vLevels;
    Vec_Ptr_t * vLevel;
    Vec_Int_t * vResult;
    int i, k;
    vLevels = Vec_VecStart( 100 );
    // make sure levels are assigned
    Gia_ManForEachAnd( p, pObj, i )
        assert( Gia_ObjLevel(p, pObj) > 0 );
    // add CO nodes based on the level of their fanin
    Gia_ManForEachCo( p, pObj, i )
        Vec_VecPush( vLevels, Gia_ObjLevel(p, Gia_ObjFanin0(pObj)), pObj );
    // add other nodes based on their level
    Gia_ManForEachObj( p, pObj, i )
        if ( !Gia_ObjIsCo(pObj) )
            Vec_VecPush( vLevels, Gia_ObjLevel(p, pObj), pObj );
    // put the nodes in the reverse topological order
    vResult = Vec_IntAlloc( Gia_ManObjNum(p) );
    Vec_VecForEachLevelReverse( vLevels, vLevel, i )
        Vec_PtrForEachEntry( Gia_Obj_t *, vLevel, pObj, k )
            Vec_IntPush( vResult, Gia_ObjId(p, pObj) );
    Vec_VecFree( vLevels );
    return vResult;
}

////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_IMPL_END

