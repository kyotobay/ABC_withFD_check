/**CFile****************************************************************

  FileName    [ioaReadAiger.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [Command processing package.]

  Synopsis    [Procedures to read binary AIGER format developed by
  Armin Biere, Johannes Kepler University (http://fmv.jku.at/)]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - December 16, 2006.]

  Revision    [$Id: ioaReadAiger.c,v 1.00 2006/12/16 00:00:00 alanmi Exp $]

***********************************************************************/

#include "ioa.h"

ABC_NAMESPACE_IMPL_START


////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    [Extracts one unsigned AIG edge from the input buffer.]

  Description [This procedure is a slightly modified version of Armin Biere's
  procedure "unsigned decode (FILE * file)". ]
  
  SideEffects [Updates the current reading position.]

  SeeAlso     []

***********************************************************************/
unsigned Ioa_ReadAigerDecode( char ** ppPos )
{
    unsigned x = 0, i = 0;
    unsigned char ch;

//    while ((ch = getnoneofch (file)) & 0x80)
    while ((ch = *(*ppPos)++) & 0x80)
        x |= (ch & 0x7f) << (7 * i++);

    return x | (ch << (7 * i));
}

/**Function*************************************************************

  Synopsis    [Decodes the encoded array of literals.]

  Description []
  
  SideEffects []

  SeeAlso     []

***********************************************************************/
Vec_Int_t * Ioa_WriteDecodeLiterals( char ** ppPos, int nEntries )
{
    Vec_Int_t * vLits;
    int Lit, LitPrev, Diff, i;
    vLits = Vec_IntAlloc( nEntries );
    LitPrev = Ioa_ReadAigerDecode( ppPos );
    Vec_IntPush( vLits, LitPrev );
    for ( i = 1; i < nEntries; i++ )
    {
//        Diff = Lit - LitPrev;
//        Diff = (Lit < LitPrev)? -Diff : Diff;
//        Diff = ((2 * Diff) << 1) | (int)(Lit < LitPrev);
        Diff = Ioa_ReadAigerDecode( ppPos );
        Diff = (Diff & 1)? -(Diff >> 1) : Diff >> 1;
        Lit  = Diff + LitPrev;
        Vec_IntPush( vLits, Lit );
        LitPrev = Lit;
    }
    return vLits;
}


/**Function*************************************************************

  Synopsis    [Reads the AIG in from the memory buffer.]

  Description [The buffer constains the AIG in AIGER format. The size gives
  the number of bytes in the buffer. The buffer is allocated by the user 
  and not deallocated by this procedure.]
  
  SideEffects []

  SeeAlso     []

***********************************************************************/
Aig_Man_t * Ioa_ReadAigerFromMemory( char * pContents, int nFileSize, int fCheck )
{
    Vec_Int_t * vLits = NULL;
    Vec_Ptr_t * vNodes, * vDrivers;//, * vTerms;
    Aig_Obj_t * pObj, * pNode0, * pNode1;
    Aig_Man_t * pNew;
    int nTotal, nInputs, nOutputs, nLatches, nAnds, i;//, iTerm, nDigits;
    char * pDrivers, * pSymbols, * pCur;//, * pType;
    unsigned uLit0, uLit1, uLit;

    // check if the input file format is correct
    if ( strncmp(pContents, "aig", 3) != 0 || (pContents[3] != ' ' && pContents[3] != '2') )
    {
        fprintf( stdout, "Wrong input file format.\n" );
        return NULL;
    }

    // read the file type
    pCur = pContents;         while ( *pCur++ != ' ' );
    // read the number of objects
    nTotal = atoi( pCur );    while ( *pCur++ != ' ' );
    // read the number of inputs
    nInputs = atoi( pCur );   while ( *pCur++ != ' ' );
    // read the number of latches
    nLatches = atoi( pCur );  while ( *pCur++ != ' ' );
    // read the number of outputs
    nOutputs = atoi( pCur );  while ( *pCur++ != ' ' );
    // read the number of nodes
    nAnds = atoi( pCur );     while ( *pCur++ != '\n' );  
    // check the parameters
    if ( nTotal != nInputs + nLatches + nAnds )
    {
        fprintf( stdout, "The paramters are wrong.\n" );
        return NULL;
    }

    // allocate the empty AIG
    pNew = Aig_ManStart( nAnds );

    // prepare the array of nodes
    vNodes = Vec_PtrAlloc( 1 + nInputs + nLatches + nAnds );
    Vec_PtrPush( vNodes, Aig_ManConst0(pNew) );

    // create the PIs
    for ( i = 0; i < nInputs + nLatches; i++ )
    {
        pObj = Aig_ObjCreatePi(pNew);    
        Vec_PtrPush( vNodes, pObj );
    }
/*
    // create the POs
    for ( i = 0; i < nOutputs + nLatches; i++ )
    {
        pObj = Aig_ObjCreatePo(pNew);   
    }
*/
    // create the latches
    pNew->nRegs = nLatches;
/*
    nDigits = Ioa_Base10Log( nLatches );
    for ( i = 0; i < nLatches; i++ )
    {
        pObj = Aig_ObjCreateLatch(pNew);
        Aig_LatchSetInit0( pObj );
        pNode0 = Aig_ObjCreateBi(pNew);
        pNode1 = Aig_ObjCreateBo(pNew);
        Aig_ObjAddFanin( pObj, pNode0 );
        Aig_ObjAddFanin( pNode1, pObj );
        Vec_PtrPush( vNodes, pNode1 );
        // assign names to latch and its input
//        Aig_ObjAssignName( pObj, Aig_ObjNameDummy("_L", i, nDigits), NULL );
//        printf( "Creating latch %s with input %d and output %d.\n", Aig_ObjName(pObj), pNode0->Id, pNode1->Id );
    } 
*/ 

    // remember the beginning of latch/PO literals
    pDrivers = pCur;
    if ( pContents[3] == ' ' ) // standard AIGER
    {
        // scroll to the beginning of the binary data
        for ( i = 0; i < nLatches + nOutputs; )
            if ( *pCur++ == '\n' )
                i++;
    }
    else // modified AIGER
    {
        vLits = Ioa_WriteDecodeLiterals( &pCur, nLatches + nOutputs );
    }

    // create the AND gates
//    pProgress = Bar_ProgressStart( stdout, nAnds );
    for ( i = 0; i < nAnds; i++ )
    {
//        Bar_ProgressUpdate( pProgress, i, NULL );
        uLit = ((i + 1 + nInputs + nLatches) << 1);
        uLit1 = uLit  - Ioa_ReadAigerDecode( &pCur );
        uLit0 = uLit1 - Ioa_ReadAigerDecode( &pCur );
//        assert( uLit1 > uLit0 );
        pNode0 = Aig_NotCond( (Aig_Obj_t *)Vec_PtrEntry(vNodes, uLit0 >> 1), uLit0 & 1 );
        pNode1 = Aig_NotCond( (Aig_Obj_t *)Vec_PtrEntry(vNodes, uLit1 >> 1), uLit1 & 1 );
        assert( Vec_PtrSize(vNodes) == i + 1 + nInputs + nLatches );
        Vec_PtrPush( vNodes, Aig_And(pNew, pNode0, pNode1) );
    }
//    Bar_ProgressStop( pProgress );

    // remember the place where symbols begin
    pSymbols = pCur;

    // read the latch driver literals
    vDrivers = Vec_PtrAlloc( nLatches + nOutputs );
    if ( pContents[3] == ' ' ) // standard AIGER
    {
        pCur = pDrivers;
        for ( i = 0; i < nLatches; i++ )
        {
            uLit0 = atoi( pCur );  while ( *pCur++ != '\n' );
            pNode0 = Aig_NotCond( (Aig_Obj_t *)Vec_PtrEntry(vNodes, uLit0 >> 1), (uLit0 & 1) );//^ (uLit0 < 2) );
            Vec_PtrPush( vDrivers, pNode0 );
        }
        // read the PO driver literals
        for ( i = 0; i < nOutputs; i++ )
        {
            uLit0 = atoi( pCur );  while ( *pCur++ != '\n' );
            pNode0 = Aig_NotCond( (Aig_Obj_t *)Vec_PtrEntry(vNodes, uLit0 >> 1), (uLit0 & 1) );//^ (uLit0 < 2) );
            Vec_PtrPush( vDrivers, pNode0 );
        }

    }
    else
    {
        // read the latch driver literals
        for ( i = 0; i < nLatches; i++ )
        {
            uLit0 = Vec_IntEntry( vLits, i );
            pNode0 = Aig_NotCond( (Aig_Obj_t *)Vec_PtrEntry(vNodes, uLit0 >> 1), (uLit0 & 1) );//^ (uLit0 < 2) );
            Vec_PtrPush( vDrivers, pNode0 );
        }
        // read the PO driver literals
        for ( i = 0; i < nOutputs; i++ )
        {
            uLit0 = Vec_IntEntry( vLits, i+nLatches );
            pNode0 = Aig_NotCond( (Aig_Obj_t *)Vec_PtrEntry(vNodes, uLit0 >> 1), (uLit0 & 1) );//^ (uLit0 < 2) );
            Vec_PtrPush( vDrivers, pNode0 );
        }
        Vec_IntFree( vLits );
    }

    // create the POs
    for ( i = 0; i < nOutputs; i++ )
        Aig_ObjCreatePo( pNew, (Aig_Obj_t *)Vec_PtrEntry(vDrivers, nLatches + i) );
    for ( i = 0; i < nLatches; i++ )
        Aig_ObjCreatePo( pNew, (Aig_Obj_t *)Vec_PtrEntry(vDrivers, i) );
    Vec_PtrFree( vDrivers );

/*
    // read the names if present
    pCur = pSymbols;
    if ( *pCur != 'c' )
    {
        int Counter = 0;
        while ( pCur < pContents + nFileSize && *pCur != 'c' )
        {
            // get the terminal type
            pType = pCur;
            if ( *pCur == 'i' )
                vTerms = pNew->vPis;
            else if ( *pCur == 'l' )
                vTerms = pNew->vBoxes;
            else if ( *pCur == 'o' )
                vTerms = pNew->vPos;
            else
            {
                fprintf( stdout, "Wrong terminal type.\n" );
                return NULL;
            }
            // get the terminal number
            iTerm = atoi( ++pCur );  while ( *pCur++ != ' ' );
            // get the node
            if ( iTerm >= Vec_PtrSize(vTerms) )
            {
                fprintf( stdout, "The number of terminal is out of bound.\n" );
                return NULL;
            }
            pObj = Vec_PtrEntry( vTerms, iTerm );
            if ( *pType == 'l' )
                pObj = Aig_ObjFanout0(pObj);
            // assign the name
            pName = pCur;          while ( *pCur++ != '\n' );
            // assign this name 
            *(pCur-1) = 0;
            Aig_ObjAssignName( pObj, pName, NULL );
            if ( *pType == 'l' )
            {
                Aig_ObjAssignName( Aig_ObjFanin0(pObj), Aig_ObjName(pObj), "L" );
                Aig_ObjAssignName( Aig_ObjFanin0(Aig_ObjFanin0(pObj)), Aig_ObjName(pObj), "_in" );
            }
            // mark the node as named
            pObj->pCopy = (Aig_Obj_t *)Aig_ObjName(pObj);
        } 

        // assign the remaining names
        Aig_ManForEachPi( pNew, pObj, i )
        {
            if ( pObj->pCopy ) continue;
            Aig_ObjAssignName( pObj, Aig_ObjName(pObj), NULL );
            Counter++;
        }
        Aig_ManForEachLatchOutput( pNew, pObj, i )
        {
            if ( pObj->pCopy ) continue;
            Aig_ObjAssignName( pObj, Aig_ObjName(pObj), NULL );
            Aig_ObjAssignName( Aig_ObjFanin0(pObj), Aig_ObjName(pObj), "L" );
            Aig_ObjAssignName( Aig_ObjFanin0(Aig_ObjFanin0(pObj)), Aig_ObjName(pObj), "_in" );
            Counter++;
        }
        Aig_ManForEachPo( pNew, pObj, i )
        {
            if ( pObj->pCopy ) continue;
            Aig_ObjAssignName( pObj, Aig_ObjName(pObj), NULL );
            Counter++;
        }
        if ( Counter )
            printf( "Ioa_ReadAiger(): Added %d default names for nameless I/O/register objects.\n", Counter );
    }
    else
    {
//        printf( "Ioa_ReadAiger(): I/O/register names are not given. Generating short names.\n" );
        Aig_ManShortNames( pNew );
    }
*/
    pCur = pSymbols;
    if ( pCur + 1 < pContents + nFileSize && *pCur == 'c' )
    {
        pCur++;
        if ( *pCur == 'n' )
        {
            pCur++;
            // read model name
            ABC_FREE( pNew->pName );
            pNew->pName = Aig_UtilStrsav( pCur );
        }
    }

    // skipping the comments
    Vec_PtrFree( vNodes );

    // remove the extra nodes
    Aig_ManCleanup( pNew );
    Aig_ManSetRegNum( pNew, Aig_ManRegNum(pNew) );

    // check the result
    if ( fCheck && !Aig_ManCheck( pNew ) )
    {
        printf( "Ioa_ReadAiger: The network check has failed.\n" );
        Aig_ManStop( pNew );
        return NULL;
    }
    return pNew;
}

/**Function*************************************************************

  Synopsis    [Reads the AIG in the binary AIGER format.]

  Description []
  
  SideEffects []

  SeeAlso     []

***********************************************************************/
Aig_Man_t * Ioa_ReadAiger( char * pFileName, int fCheck )
{
    FILE * pFile;
    Aig_Man_t * pNew;
    char * pName, * pContents;
    int nFileSize;

    // read the file into the buffer
    nFileSize = Ioa_FileSize( pFileName );
    pFile = fopen( pFileName, "rb" );
    pContents = ABC_ALLOC( char, nFileSize );
    fread( pContents, nFileSize, 1, pFile );
    fclose( pFile );

    pNew = Ioa_ReadAigerFromMemory( pContents, nFileSize, fCheck );
    ABC_FREE( pContents );
    if ( pNew )
    {
        pName = Ioa_FileNameGeneric( pFileName );
        pNew->pName = Aig_UtilStrsav( pName );
//        pNew->pSpec = Ioa_UtilStrsav( pFileName );
        ABC_FREE( pName );
    }
    return pNew;
}


////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_IMPL_END

