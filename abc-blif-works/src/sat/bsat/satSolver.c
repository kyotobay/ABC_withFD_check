/**************************************************************************************************
MiniSat -- Copyright (c) 2005, Niklas Sorensson
http://www.cs.chalmers.se/Cs/Research/FormalMethods/MiniSat/

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**************************************************************************************************/
// Modified to compile with MS Visual Studio 6.0 by Alan Mishchenko

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#include "satSolver.h"
#include "satStore.h"

ABC_NAMESPACE_IMPL_START

#define SAT_USE_ANALYZE_FINAL

/*
extern int Sto_ManAddClause( void * p, lit * pBeg, lit * pEnd );
extern int Sto_ManAddClause( void * p, lit * pBeg, lit * pEnd );
extern int Sto_ManAddClause( void * p, lit * pBeg, lit * pEnd );
extern int Sto_ManAddClause( void * p, lit * pBeg, lit * pEnd );
extern void * Sto_ManAlloc();
extern void Sto_ManDumpClauses( void * p, char * pFileName );
extern void Sto_ManFree( void * p );
extern int Sto_ManChangeLastClause( void * p );
extern void Sto_ManMarkRoots( void * p );
extern void Sto_ManMarkClausesA( void * p );
*/

//#define SAT_USE_SYSTEM_MEMORY_MANAGEMENT


//=================================================================================================
// Debug:

//#define VERBOSEDEBUG

// For derivation output (verbosity level 2)
#define L_IND    "%-*d"
#define L_ind    sat_solver_dlevel(s)*3+3,sat_solver_dlevel(s)
#define L_LIT    "%sx%d"
#define L_lit(p) lit_sign(p)?"~":"", (lit_var(p))

// Just like 'assert()' but expression will be evaluated in the release version as well.
static inline void check(int expr) { assert(expr); }

static void printlits(lit* begin, lit* end)
{
    int i;
    for (i = 0; i < end - begin; i++)
        printf(L_LIT" ",L_lit(begin[i]));
}

//=================================================================================================
// Random numbers:


// Returns a random float 0 <= x < 1. Seed must never be 0.
static inline double drand(double* seed) {
    int q;
    *seed *= 1389796;
    q = (int)(*seed / 2147483647);
    *seed -= (double)q * 2147483647;
    return *seed / 2147483647; }


// Returns a random integer 0 <= x < size. Seed must never be 0.
static inline int irand(double* seed, int size) {
    return (int)(drand(seed) * size); }


//=================================================================================================
// Predeclarations:

static void sat_solver_sort(void** array, int size, int(*comp)(const void *, const void *));

//=================================================================================================
// Clause datatype + minor functions:

struct clause_t
{
    int size_learnt;
    lit lits[0];
};

static inline int   clause_size       (clause* c)          { return c->size_learnt >> 1; }
static inline lit*  clause_begin      (clause* c)          { return c->lits; }
static inline int   clause_learnt     (clause* c)          { return c->size_learnt & 1; }
static inline float clause_activity   (clause* c)          { return *((float*)&c->lits[c->size_learnt>>1]); }
static inline void  clause_setactivity(clause* c, float a) { *((float*)&c->lits[c->size_learnt>>1]) = a; }

//=================================================================================================
// Encode literals in clause pointers:

static inline clause* clause_from_lit (lit l)     { return (clause*)((ABC_PTRUINT_T)l + (ABC_PTRUINT_T)l + 1); }
static inline int     clause_is_lit   (clause* c) { return ((ABC_PTRUINT_T)c & 1);                              }
static inline lit     clause_read_lit (clause* c) { return (lit)((ABC_PTRUINT_T)c >> 1);                        }

//=================================================================================================
// Simple helpers:

static inline int     sat_solver_dlevel(sat_solver* s)            { return veci_size(&s->trail_lim); }
static inline vecp*   sat_solver_read_wlist(sat_solver* s, lit l) { return &s->wlists[l]; }
static inline void    vecp_remove(vecp* v, void* e)
{
    void** ws = vecp_begin(v);
    int    j  = 0;
    for (; ws[j] != e  ; j++);
    assert(j < vecp_size(v));
    for (; j < vecp_size(v)-1; j++) ws[j] = ws[j+1];
    vecp_resize(v,vecp_size(v)-1);
}

//=================================================================================================
// Variable order functions:

static inline void order_update(sat_solver* s, int v) // updateorder
{
    int*    orderpos = s->orderpos;
    double* activity = s->activity;
    int*    heap     = veci_begin(&s->order);
    int     i        = orderpos[v];
    int     x        = heap[i];
    int     parent   = (i - 1) / 2;

    assert(s->orderpos[v] != -1);

    while (i != 0 && activity[x] > activity[heap[parent]]){
        heap[i]           = heap[parent];
        orderpos[heap[i]] = i;
        i                 = parent;
        parent            = (i - 1) / 2;
    }
    heap[i]     = x;
    orderpos[x] = i;
}

static inline void order_assigned(sat_solver* s, int v) 
{
}

static inline void order_unassigned(sat_solver* s, int v) // undoorder
{
    int* orderpos = s->orderpos;
    if (orderpos[v] == -1){
        orderpos[v] = veci_size(&s->order);
        veci_push(&s->order,v);
        order_update(s,v);
//printf( "+%d ", v );
    }
}

static inline int  order_select(sat_solver* s, float random_var_freq) // selectvar
{
    int*    heap;
    double* activity;
    int*    orderpos;

    lbool* values = s->assigns;

    // Random decision:
    if (drand(&s->random_seed) < random_var_freq){
        int next = irand(&s->random_seed,s->size);
        assert(next >= 0 && next < s->size);
        if (values[next] == l_Undef)
            return next;
    }

    // Activity based decision:

    heap     = veci_begin(&s->order);
    activity = s->activity;
    orderpos = s->orderpos;


    while (veci_size(&s->order) > 0){
        int    next  = heap[0];
        int    size  = veci_size(&s->order)-1;
        int    x     = heap[size];

        veci_resize(&s->order,size);

        orderpos[next] = -1;

        if (size > 0){
            double act   = activity[x];

            int    i     = 0;
            int    child = 1;


            while (child < size){
                if (child+1 < size && activity[heap[child]] < activity[heap[child+1]])
                    child++;

                assert(child < size);

                if (act >= activity[heap[child]])
                    break;

                heap[i]           = heap[child];
                orderpos[heap[i]] = i;
                i                 = child;
                child             = 2 * child + 1;
            }
            heap[i]           = x;
            orderpos[heap[i]] = i;
        }

//printf( "-%d ", next );
        if (values[next] == l_Undef)
            return next;
    }

    return var_Undef;
}

//=================================================================================================
// Activity functions:

static inline void act_var_rescale(sat_solver* s) {
    double* activity = s->activity;
    int i;
    for (i = 0; i < s->size; i++)
        activity[i] *= 1e-100;
    s->var_inc *= 1e-100;
}

static inline void act_var_bump(sat_solver* s, int v) {
//    s->activity[v] += s->var_inc;
    s->activity[v] += (s->pGlobalVars? 3.0 : 1.0) * s->var_inc;
    if (s->activity[v] > 1e100)
        act_var_rescale(s);
    //printf("bump %d %f\n", v-1, activity[v]);
    if (s->orderpos[v] != -1)
        order_update(s,v);
}

static inline void act_var_bump_factor(sat_solver* s, int v) {
    s->activity[v] += (s->var_inc * s->factors[v]);
    if (s->activity[v] > 1e100)
        act_var_rescale(s);
    //printf("bump %d %f\n", v-1, activity[v]);
    if (s->orderpos[v] != -1)
        order_update(s,v);
}

static inline void act_var_bump_global(sat_solver* s, int v) {
    s->activity[v] += (s->var_inc * 3.0 * s->pGlobalVars[v]);
    if (s->activity[v] > 1e100)
        act_var_rescale(s);
    //printf("bump %d %f\n", v-1, activity[v]);
    if (s->orderpos[v] != -1)
        order_update(s,v);
}

static inline void act_var_decay(sat_solver* s) { s->var_inc *= s->var_decay; }

static inline void act_clause_rescale(sat_solver* s) {
    clause** cs = (clause**)vecp_begin(&s->learnts);
    int i;
    for (i = 0; i < vecp_size(&s->learnts); i++){
        float a = clause_activity(cs[i]);
        clause_setactivity(cs[i], a * (float)1e-20);
    }
    s->cla_inc *= (float)1e-20;
}


static inline void act_clause_bump(sat_solver* s, clause *c) {
    float a = clause_activity(c) + s->cla_inc;
    clause_setactivity(c,a);
    if (a > 1e20) act_clause_rescale(s);
}

static inline void act_clause_decay(sat_solver* s) { s->cla_inc *= s->cla_decay; }

//=================================================================================================
// Clause functions:

/* pre: size > 1 && no variable occurs twice
 */
static clause* clause_new(sat_solver* s, lit* begin, lit* end, int learnt)
{
    int size;
    clause* c;
    int i;

    assert(end - begin > 1);
    assert(learnt >= 0 && learnt < 2);
    size           = end - begin;
//    c              = (clause*)ABC_ALLOC( char, sizeof(clause) + sizeof(lit) * size + learnt * sizeof(float));
#ifdef SAT_USE_SYSTEM_MEMORY_MANAGEMENT
    c = (clause*)ABC_ALLOC( char, sizeof(clause) + sizeof(lit) * size + learnt * sizeof(float));
#else
    c = (clause*)Sat_MmStepEntryFetch( s->pMem, sizeof(clause) + sizeof(lit) * size + learnt * sizeof(float) );
#endif

    c->size_learnt = (size << 1) | learnt;
    assert(((ABC_PTRUINT_T)c & 1) == 0);

    for (i = 0; i < size; i++)
        c->lits[i] = begin[i];

    if (learnt)
        *((float*)&c->lits[size]) = 0.0;

    assert(begin[0] >= 0);
    assert(begin[0] < s->size*2);
    assert(begin[1] >= 0);
    assert(begin[1] < s->size*2);

    assert(lit_neg(begin[0]) < s->size*2);
    assert(lit_neg(begin[1]) < s->size*2);

    //vecp_push(sat_solver_read_wlist(s,lit_neg(begin[0])),(void*)c);
    //vecp_push(sat_solver_read_wlist(s,lit_neg(begin[1])),(void*)c);

    vecp_push(sat_solver_read_wlist(s,lit_neg(begin[0])),(void*)(size > 2 ? c : clause_from_lit(begin[1])));
    vecp_push(sat_solver_read_wlist(s,lit_neg(begin[1])),(void*)(size > 2 ? c : clause_from_lit(begin[0])));

    return c;
}


static void clause_remove(sat_solver* s, clause* c)
{
    lit* lits = clause_begin(c);
    assert(lit_neg(lits[0]) < s->size*2);
    assert(lit_neg(lits[1]) < s->size*2);

    //vecp_remove(sat_solver_read_wlist(s,lit_neg(lits[0])),(void*)c);
    //vecp_remove(sat_solver_read_wlist(s,lit_neg(lits[1])),(void*)c);

    assert(lits[0] < s->size*2);
    vecp_remove(sat_solver_read_wlist(s,lit_neg(lits[0])),(void*)(clause_size(c) > 2 ? c : clause_from_lit(lits[1])));
    vecp_remove(sat_solver_read_wlist(s,lit_neg(lits[1])),(void*)(clause_size(c) > 2 ? c : clause_from_lit(lits[0])));

    if (clause_learnt(c)){
        s->stats.learnts--;
        s->stats.learnts_literals -= clause_size(c);
    }else{
        s->stats.clauses--;
        s->stats.clauses_literals -= clause_size(c);
    }

#ifdef SAT_USE_SYSTEM_MEMORY_MANAGEMENT
    ABC_FREE(c);
#else
    Sat_MmStepEntryRecycle( s->pMem, (char *)c, sizeof(clause) + sizeof(lit) * clause_size(c) + clause_learnt(c) * sizeof(float) );
#endif
}


static lbool clause_simplify(sat_solver* s, clause* c)
{
    lit*   lits   = clause_begin(c);
    lbool* values = s->assigns;
    int i;

    assert(sat_solver_dlevel(s) == 0);

    for (i = 0; i < clause_size(c); i++){
        lbool sig = !lit_sign(lits[i]); sig += sig - 1;
        if (values[lit_var(lits[i])] == sig)
            return l_True;
    }
    return l_False;
}

//=================================================================================================
// Minor (solver) functions:

void sat_solver_setnvars(sat_solver* s,int n)
{
    int var;

    if (s->cap < n){

        while (s->cap < n) s->cap = s->cap*2+1;

        s->wlists    = ABC_REALLOC(vecp,   s->wlists,   s->cap*2);
        s->activity  = ABC_REALLOC(double, s->activity, s->cap);
        s->factors   = ABC_REALLOC(double, s->factors,  s->cap);
        s->assigns   = ABC_REALLOC(lbool,  s->assigns,  s->cap);
        s->orderpos  = ABC_REALLOC(int,    s->orderpos, s->cap);
        s->reasons   = ABC_REALLOC(clause*,s->reasons,  s->cap);
        s->levels    = ABC_REALLOC(int,    s->levels,   s->cap);
        s->tags      = ABC_REALLOC(lbool,  s->tags,     s->cap);
        s->trail     = ABC_REALLOC(lit,    s->trail,    s->cap);
        s->polarity  = ABC_REALLOC(char,   s->polarity, s->cap);
    }

    for (var = s->size; var < n; var++){
        vecp_new(&s->wlists[2*var]);
        vecp_new(&s->wlists[2*var+1]);
        s->activity [var] = 0;
        s->factors  [var] = 0;
        s->assigns  [var] = l_Undef;
        s->orderpos [var] = veci_size(&s->order);
        s->reasons  [var] = (clause*)0;
        s->levels   [var] = 0;
        s->tags     [var] = l_Undef;
        s->polarity [var] = 0;
        
        /* does not hold because variables enqueued at top level will not be reinserted in the heap
           assert(veci_size(&s->order) == var); 
         */
        veci_push(&s->order,var);
        order_update(s, var);
    }

    s->size = n > s->size ? n : s->size;
}


static inline int enqueue(sat_solver* s, lit l, clause* from)
{
    lbool* values = s->assigns;
    int    v      = lit_var(l);
    lbool  val    = values[v];
#ifdef VERBOSEDEBUG
    printf(L_IND"enqueue("L_LIT")\n", L_ind, L_lit(l));
#endif

    lbool sig = !lit_sign(l); sig += sig - 1;
    if (val != l_Undef){
        return val == sig;
    }else{
        // New fact -- store it.
#ifdef VERBOSEDEBUG
        printf(L_IND"bind("L_LIT")\n", L_ind, L_lit(l));
#endif
        int*     levels  = s->levels;
        clause** reasons = s->reasons;

        values [v] = sig;
        levels [v] = sat_solver_dlevel(s);
        reasons[v] = from;
        s->trail[s->qtail++] = l;

        order_assigned(s, v);
        return true;
    }
}


static inline int assume(sat_solver* s, lit l){
    assert(s->qtail == s->qhead);
    assert(s->assigns[lit_var(l)] == l_Undef);
#ifdef VERBOSEDEBUG
    printf(L_IND"assume("L_LIT")\n", L_ind, L_lit(l));
#endif
    veci_push(&s->trail_lim,s->qtail);
    return enqueue(s,l,(clause*)0);
}


static void sat_solver_canceluntil(sat_solver* s, int level) {
    lit*     trail;   
    lbool*   values;  
    clause** reasons; 
    int      bound;
    int      lastLev;
    int      c;
    
    if (sat_solver_dlevel(s) <= level)
        return;

    trail   = s->trail;
    values  = s->assigns;
    reasons = s->reasons;
    bound   = (veci_begin(&s->trail_lim))[level];
    lastLev = (veci_begin(&s->trail_lim))[veci_size(&s->trail_lim)-1];

    ////////////////////////////////////////
    // added to cancel all assignments
//    if ( level == -1 )
//        bound = 0;
    ////////////////////////////////////////

    for (c = s->qtail-1; c >= bound; c--) {
        int     x  = lit_var(trail[c]);
        values [x] = l_Undef;
        reasons[x] = (clause*)0;
        if ( c < lastLev )
            s->polarity[x] = !lit_sign(trail[c]);
    }

    for (c = s->qhead-1; c >= bound; c--)
        order_unassigned(s,lit_var(trail[c]));

    s->qhead = s->qtail = bound;
    veci_resize(&s->trail_lim,level);
}

static void sat_solver_record(sat_solver* s, veci* cls)
{
    lit*    begin = veci_begin(cls);
    lit*    end   = begin + veci_size(cls);
    clause* c     = (veci_size(cls) > 1) ? clause_new(s,begin,end,1) : (clause*)0;
    enqueue(s,*begin,c);

    ///////////////////////////////////
    // add clause to internal storage
    if ( s->pStore )
    {
        int RetValue = Sto_ManAddClause( (Sto_Man_t *)s->pStore, begin, end );
        assert( RetValue );
    }
    ///////////////////////////////////

    assert(veci_size(cls) > 0);

    if (c != 0) {
        vecp_push(&s->learnts,c);
        act_clause_bump(s,c);
        s->stats.learnts++;
        s->stats.learnts_literals += veci_size(cls);
    }
}


static double sat_solver_progress(sat_solver* s)
{
    lbool*  values = s->assigns;
    int*    levels = s->levels;
    int     i;

    double  progress = 0;
    double  F        = 1.0 / s->size;
    for (i = 0; i < s->size; i++)
        if (values[i] != l_Undef)
            progress += pow(F, levels[i]);
    return progress / s->size;
}

//=================================================================================================
// Major methods:

static int sat_solver_lit_removable(sat_solver* s, lit l, int minl)
{
    lbool*   tags    = s->tags;
    clause** reasons = s->reasons;
    int*     levels  = s->levels;
    int      top     = veci_size(&s->tagged);

    assert(lit_var(l) >= 0 && lit_var(l) < s->size);
    assert(reasons[lit_var(l)] != 0);
    veci_resize(&s->stack,0);
    veci_push(&s->stack,lit_var(l));

    while (veci_size(&s->stack) > 0){
        clause* c;
        int v = veci_begin(&s->stack)[veci_size(&s->stack)-1];
        assert(v >= 0 && v < s->size);
        veci_resize(&s->stack,veci_size(&s->stack)-1);
        assert(reasons[v] != 0);
        c    = reasons[v];

        if (clause_is_lit(c)){
            int v = lit_var(clause_read_lit(c));
            if (tags[v] == l_Undef && levels[v] != 0){
                if (reasons[v] != 0 && ((1 << (levels[v] & 31)) & minl)){
                    veci_push(&s->stack,v);
                    tags[v] = l_True;
                    veci_push(&s->tagged,v);
                }else{
                    int* tagged = veci_begin(&s->tagged);
                    int j;
                    for (j = top; j < veci_size(&s->tagged); j++)
                        tags[tagged[j]] = l_Undef;
                    veci_resize(&s->tagged,top);
                    return false;
                }
            }
        }else{
            lit*    lits = clause_begin(c);
            int     i, j;

            for (i = 1; i < clause_size(c); i++){
                int v = lit_var(lits[i]);
                if (tags[v] == l_Undef && levels[v] != 0){
                    if (reasons[v] != 0 && ((1 << (levels[v] & 31)) & minl)){

                        veci_push(&s->stack,lit_var(lits[i]));
                        tags[v] = l_True;
                        veci_push(&s->tagged,v);
                    }else{
                        int* tagged = veci_begin(&s->tagged);
                        for (j = top; j < veci_size(&s->tagged); j++)
                            tags[tagged[j]] = l_Undef;
                        veci_resize(&s->tagged,top);
                        return false;
                    }
                }
            }
        }
    }

    return true;
}


/*_________________________________________________________________________________________________
|
|  analyzeFinal : (p : Lit)  ->  [void]
|  
|  Description:
|    Specialized analysis procedure to express the final conflict in terms of assumptions.
|    Calculates the (possibly empty) set of assumptions that led to the assignment of 'p', and
|    stores the result in 'out_conflict'.
|________________________________________________________________________________________________@*/
/*
void Solver::analyzeFinal(Clause* confl, bool skip_first)
{
    // -- NOTE! This code is relatively untested. Please report bugs!
    conflict.clear();
    if (root_level == 0) return;

    vec<char>& seen  = analyze_seen;
    for (int i = skip_first ? 1 : 0; i < confl->size(); i++){
        Var x = var((*confl)[i]);
        if (level[x] > 0)
            seen[x] = 1;
    }

    int start = (root_level >= trail_lim.size()) ? trail.size()-1 : trail_lim[root_level];
    for (int i = start; i >= trail_lim[0]; i--){
        Var     x = var(trail[i]);
        if (seen[x]){
            GClause r = reason[x];
            if (r == GClause_NULL){
                assert(level[x] > 0);
                conflict.push(~trail[i]);
            }else{
                if (r.isLit()){
                    Lit p = r.lit();
                    if (level[var(p)] > 0)
                        seen[var(p)] = 1;
                }else{
                    Clause& c = *r.clause();
                    for (int j = 1; j < c.size(); j++)
                        if (level[var(c[j])] > 0)
                            seen[var(c[j])] = 1;
                }
            }
            seen[x] = 0;
        }
    }
}
*/

#ifdef SAT_USE_ANALYZE_FINAL

static void sat_solver_analyze_final(sat_solver* s, clause* conf, int skip_first)
{
    int i, j, start;
    veci_resize(&s->conf_final,0);
    if ( s->root_level == 0 )
        return;
    assert( veci_size(&s->tagged) == 0 );
//    assert( s->tags[lit_var(p)] == l_Undef );
//    s->tags[lit_var(p)] = l_True;
    for (i = skip_first ? 1 : 0; i < clause_size(conf); i++)
    {
        int x = lit_var(clause_begin(conf)[i]);
        if (s->levels[x] > 0)
        {
            s->tags[x] = l_True;
            veci_push(&s->tagged,x);
        }
    }

    start = (s->root_level >= veci_size(&s->trail_lim))? s->qtail-1 : (veci_begin(&s->trail_lim))[s->root_level];
    for (i = start; i >= (veci_begin(&s->trail_lim))[0]; i--){
        int x = lit_var(s->trail[i]);
        if (s->tags[x] == l_True){
            if (s->reasons[x] == NULL){
                assert(s->levels[x] > 0);
                veci_push(&s->conf_final,lit_neg(s->trail[i]));
            }else{
                clause* c = s->reasons[x];
                if (clause_is_lit(c)){
                    lit q = clause_read_lit(c);
                    assert(lit_var(q) >= 0 && lit_var(q) < s->size);
                    if (s->levels[lit_var(q)] > 0)
                    {
                        s->tags[lit_var(q)] = l_True;
                        veci_push(&s->tagged,lit_var(q));
                    }
                }
                else{
                    int* lits = clause_begin(c);
                    for (j = 1; j < clause_size(c); j++)
                        if (s->levels[lit_var(lits[j])] > 0)
                        {
                            s->tags[lit_var(lits[j])] = l_True;
                            veci_push(&s->tagged,lit_var(lits[j]));
                        }
                }
            }
        }
    }
    for (i = 0; i < veci_size(&s->tagged); i++)
        s->tags[veci_begin(&s->tagged)[i]] = l_Undef;
    veci_resize(&s->tagged,0);
}

#endif


static void sat_solver_analyze(sat_solver* s, clause* c, veci* learnt)
{
    lit*     trail   = s->trail;
    lbool*   tags    = s->tags;
    clause** reasons = s->reasons;
    int*     levels  = s->levels;
    int      cnt     = 0;
    lit      p       = lit_Undef;
    int      ind     = s->qtail-1;
    lit*     lits;
    int      i, j, minl;
    int*     tagged;

    veci_push(learnt,lit_Undef);

    do{
        assert(c != 0);

        if (clause_is_lit(c)){
            lit q = clause_read_lit(c);
            assert(lit_var(q) >= 0 && lit_var(q) < s->size);
            if (tags[lit_var(q)] == l_Undef && levels[lit_var(q)] > 0){
                tags[lit_var(q)] = l_True;
                veci_push(&s->tagged,lit_var(q));
                act_var_bump(s,lit_var(q));
                if (levels[lit_var(q)] == sat_solver_dlevel(s))
                    cnt++;
                else
                    veci_push(learnt,q);
            }
        }else{

            if (clause_learnt(c))
                act_clause_bump(s,c);

            lits = clause_begin(c);
            //printlits(lits,lits+clause_size(c)); printf("\n");
            for (j = (p == lit_Undef ? 0 : 1); j < clause_size(c); j++){
                lit q = lits[j];
                assert(lit_var(q) >= 0 && lit_var(q) < s->size);
                if (tags[lit_var(q)] == l_Undef && levels[lit_var(q)] > 0){
                    tags[lit_var(q)] = l_True;
                    veci_push(&s->tagged,lit_var(q));
                    act_var_bump(s,lit_var(q));
                    if (levels[lit_var(q)] == sat_solver_dlevel(s))
                        cnt++;
                    else
                        veci_push(learnt,q);
                }
            }
        }

        while (tags[lit_var(trail[ind--])] == l_Undef);

        p = trail[ind+1];
        c = reasons[lit_var(p)];
        cnt--;

    }while (cnt > 0);

    *veci_begin(learnt) = lit_neg(p);

    lits = veci_begin(learnt);
    minl = 0;
    for (i = 1; i < veci_size(learnt); i++){
        int lev = levels[lit_var(lits[i])];
        minl    |= 1 << (lev & 31);
    }

    // simplify (full)
    for (i = j = 1; i < veci_size(learnt); i++){
        if (reasons[lit_var(lits[i])] == 0 || !sat_solver_lit_removable(s,lits[i],minl))
            lits[j++] = lits[i];
    }

    // update size of learnt + statistics
    s->stats.max_literals += veci_size(learnt);
    veci_resize(learnt,j);
    s->stats.tot_literals += j;

    // clear tags
    tagged = veci_begin(&s->tagged);
    for (i = 0; i < veci_size(&s->tagged); i++)
        tags[tagged[i]] = l_Undef;
    veci_resize(&s->tagged,0);

#ifdef DEBUG
    for (i = 0; i < s->size; i++)
        assert(tags[i] == l_Undef);
#endif

#ifdef VERBOSEDEBUG
    printf(L_IND"Learnt {", L_ind);
    for (i = 0; i < veci_size(learnt); i++) printf(" "L_LIT, L_lit(lits[i]));
#endif
    if (veci_size(learnt) > 1){
        int max_i = 1;
        int max   = levels[lit_var(lits[1])];
        lit tmp;

        for (i = 2; i < veci_size(learnt); i++)
            if (levels[lit_var(lits[i])] > max){
                max   = levels[lit_var(lits[i])];
                max_i = i;
            }

        tmp         = lits[1];
        lits[1]     = lits[max_i];
        lits[max_i] = tmp;
    }
#ifdef VERBOSEDEBUG
    {
        int lev = veci_size(learnt) > 1 ? levels[lit_var(lits[1])] : 0;
        printf(" } at level %d\n", lev);
    }
#endif
}


clause* sat_solver_propagate(sat_solver* s)
{
    lbool*  values = s->assigns;
    clause* confl  = (clause*)0;
    lit*    lits;

    //printf("sat_solver_propagate\n");
    while (confl == 0 && s->qtail - s->qhead > 0){
        lit  p  = s->trail[s->qhead++];
        vecp* ws = sat_solver_read_wlist(s,p);
        clause **begin = (clause**)vecp_begin(ws);
        clause **end   = begin + vecp_size(ws);
        clause **i, **j;

        s->stats.propagations++;
        s->simpdb_props--;

        //printf("checking lit %d: "L_LIT"\n", veci_size(ws), L_lit(p));
        for (i = j = begin; i < end; ){
            if (clause_is_lit(*i)){
//                s->stats.inspects2++;
                *j++ = *i;
                if (!enqueue(s,clause_read_lit(*i),clause_from_lit(p))){
                    confl = s->binary;
                    (clause_begin(confl))[1] = lit_neg(p);
                    (clause_begin(confl))[0] = clause_read_lit(*i++);
                    // Copy the remaining watches:
//                    s->stats.inspects2 += end - i;
                    while (i < end)
                        *j++ = *i++;
                }
            }else{
                lit false_lit;
                lbool sig;

                lits = clause_begin(*i);

                // Make sure the false literal is data[1]:
                false_lit = lit_neg(p);
                if (lits[0] == false_lit){
                    lits[0] = lits[1];
                    lits[1] = false_lit;
                }
                assert(lits[1] == false_lit);
                //printf("checking clause: "); printlits(lits, lits+clause_size(*i)); printf("\n");

                // If 0th watch is true, then clause is already satisfied.
                sig = !lit_sign(lits[0]); sig += sig - 1;
                if (values[lit_var(lits[0])] == sig){
                    *j++ = *i;
                }else{
                    // Look for new watch:
                    lit* stop = lits + clause_size(*i);
                    lit* k;
                    for (k = lits + 2; k < stop; k++){
                        lbool sig = lit_sign(*k); sig += sig - 1;
                        if (values[lit_var(*k)] != sig){
                            lits[1] = *k;
                            *k = false_lit;
                            vecp_push(sat_solver_read_wlist(s,lit_neg(lits[1])),*i);
                            goto next; }
                    }

                    *j++ = *i;
                    // Clause is unit under assignment:
                    if (!enqueue(s,lits[0], *i)){
                        confl = *i++;
                        // Copy the remaining watches:
//                        s->stats.inspects2 += end - i;
                        while (i < end)
                            *j++ = *i++;
                    }
                }
            }
        next:
            i++;
        }

        s->stats.inspects += j - (clause**)vecp_begin(ws);
        vecp_resize(ws,j - (clause**)vecp_begin(ws));
    }

    return confl;
}

static int clause_cmp (const void* x, const void* y) {
    return clause_size((clause*)x) > 2 && (clause_size((clause*)y) == 2 || clause_activity((clause*)x) < clause_activity((clause*)y)) ? -1 : 1; }

void sat_solver_reducedb(sat_solver* s)
{
    int      i, j;
    double   extra_lim = s->cla_inc / vecp_size(&s->learnts); // Remove any clause below this activity
    clause** learnts = (clause**)vecp_begin(&s->learnts);
    clause** reasons = s->reasons;

    sat_solver_sort(vecp_begin(&s->learnts), vecp_size(&s->learnts), &clause_cmp);

    for (i = j = 0; i < vecp_size(&s->learnts) / 2; i++){
        if (clause_size(learnts[i]) > 2 && reasons[lit_var(*clause_begin(learnts[i]))] != learnts[i])
            clause_remove(s,learnts[i]);
        else
            learnts[j++] = learnts[i];
    }
    for (; i < vecp_size(&s->learnts); i++){
        if (clause_size(learnts[i]) > 2 && reasons[lit_var(*clause_begin(learnts[i]))] != learnts[i] && clause_activity(learnts[i]) < extra_lim)
            clause_remove(s,learnts[i]);
        else
            learnts[j++] = learnts[i];
    }

    //printf("reducedb deleted %d\n", vecp_size(&s->learnts) - j);


    vecp_resize(&s->learnts,j);
}

static lbool sat_solver_search(sat_solver* s, ABC_INT64_T nof_conflicts, ABC_INT64_T nof_learnts)
{
    int*    levels          = s->levels;
    double  var_decay       = 0.95;
    double  clause_decay    = 0.999;
    double  random_var_freq = 0.02;

    ABC_INT64_T  conflictC       = 0;
    veci    learnt_clause;
    int     i;

    assert(s->root_level == sat_solver_dlevel(s));

    s->nRestarts++;
    s->stats.starts++;
    s->var_decay = (float)(1 / var_decay   );
    s->cla_decay = (float)(1 / clause_decay);
    veci_resize(&s->model,0);
    veci_new(&learnt_clause);

    // use activity factors in every even restart
    if ( (s->nRestarts & 1) && veci_size(&s->act_vars) > 0 )
//    if ( veci_size(&s->act_vars) > 0 )
        for ( i = 0; i < s->act_vars.size; i++ )
            act_var_bump_factor(s, s->act_vars.ptr[i]);

    // use activity factors in every restart
    if ( s->pGlobalVars && veci_size(&s->act_vars) > 0 )
        for ( i = 0; i < s->act_vars.size; i++ )
            act_var_bump_global(s, s->act_vars.ptr[i]);

    for (;;){
        clause* confl = sat_solver_propagate(s);
        if (confl != 0){
            // CONFLICT
            int blevel;

#ifdef VERBOSEDEBUG
            printf(L_IND"**CONFLICT**\n", L_ind);
#endif
            s->stats.conflicts++; conflictC++;
            if (sat_solver_dlevel(s) == s->root_level){
#ifdef SAT_USE_ANALYZE_FINAL
                sat_solver_analyze_final(s, confl, 0);
#endif
                veci_delete(&learnt_clause);
                return l_False;
            }

            veci_resize(&learnt_clause,0);
            sat_solver_analyze(s, confl, &learnt_clause);
            blevel = veci_size(&learnt_clause) > 1 ? levels[lit_var(veci_begin(&learnt_clause)[1])] : s->root_level;
            blevel = s->root_level > blevel ? s->root_level : blevel;
            sat_solver_canceluntil(s,blevel);
            sat_solver_record(s,&learnt_clause);
#ifdef SAT_USE_ANALYZE_FINAL
//            if (learnt_clause.size() == 1) level[var(learnt_clause[0])] = 0;    // (this is ugly (but needed for 'analyzeFinal()') -- in future versions, we will backtrack past the 'root_level' and redo the assumptions)
            if ( learnt_clause.size == 1 ) s->levels[lit_var(learnt_clause.ptr[0])] = 0;
#endif
            act_var_decay(s);
            act_clause_decay(s);

        }else{
            // NO CONFLICT
            int next;

            if (nof_conflicts >= 0 && conflictC >= nof_conflicts){
                // Reached bound on number of conflicts:
                s->progress_estimate = sat_solver_progress(s);
                sat_solver_canceluntil(s,s->root_level);
                veci_delete(&learnt_clause);
                return l_Undef; }

            if ( (s->nConfLimit && s->stats.conflicts > s->nConfLimit) ||
//                 (s->nInsLimit  && s->stats.inspects  > s->nInsLimit) )
                 (s->nInsLimit  && s->stats.propagations > s->nInsLimit) )
            {
                // Reached bound on number of conflicts:
                s->progress_estimate = sat_solver_progress(s);
                sat_solver_canceluntil(s,s->root_level);
                veci_delete(&learnt_clause);
                return l_Undef; 
            }

            if (sat_solver_dlevel(s) == 0 && !s->fSkipSimplify)
                // Simplify the set of problem clauses:
                sat_solver_simplify(s);

            if (nof_learnts >= 0 && vecp_size(&s->learnts) - s->qtail >= nof_learnts)
                // Reduce the set of learnt clauses:
                sat_solver_reducedb(s);

            // New variable decision:
            s->stats.decisions++;
            next = order_select(s,(float)random_var_freq);

            if (next == var_Undef){
                // Model found:
                lbool* values = s->assigns;
                int i;
                veci_resize(&s->model, 0);
                for (i = 0; i < s->size; i++) 
                    veci_push(&s->model,(int)values[i]);
                sat_solver_canceluntil(s,s->root_level);
                veci_delete(&learnt_clause);

                /*
                veci apa; veci_new(&apa);
                for (i = 0; i < s->size; i++) 
                    veci_push(&apa,(int)(s->model.ptr[i] == l_True ? toLit(i) : lit_neg(toLit(i))));
                printf("model: "); printlits((lit*)apa.ptr, (lit*)apa.ptr + veci_size(&apa)); printf("\n");
                veci_delete(&apa);
                */

                return l_True;
            }

            if ( s->polarity[next] ) // positive polarity
                assume(s,toLit(next));
            else
                assume(s,lit_neg(toLit(next)));
        }
    }

    return l_Undef; // cannot happen
}

//=================================================================================================
// External solver functions:

sat_solver* sat_solver_new(void)
{
    sat_solver* s = (sat_solver*)ABC_ALLOC( char, sizeof(sat_solver));
    memset( s, 0, sizeof(sat_solver) );

    // initialize vectors
    vecp_new(&s->clauses);
    vecp_new(&s->learnts);
    veci_new(&s->order);
    veci_new(&s->trail_lim);
    veci_new(&s->tagged);
    veci_new(&s->stack);
    veci_new(&s->model);
    veci_new(&s->act_vars);
    veci_new(&s->temp_clause);
    veci_new(&s->conf_final);

    // initialize arrays
    s->wlists    = 0;
    s->activity  = 0;
    s->factors   = 0;
    s->assigns   = 0;
    s->orderpos  = 0;
    s->reasons   = 0;
    s->levels    = 0;
    s->tags      = 0;
    s->trail     = 0;


    // initialize other vars
    s->size                   = 0;
    s->cap                    = 0;
    s->qhead                  = 0;
    s->qtail                  = 0;
    s->cla_inc                = 1;
    s->cla_decay              = 1;
    s->var_inc                = 1;
    s->var_decay              = 1;
    s->root_level             = 0;
    s->simpdb_assigns         = 0;
    s->simpdb_props           = 0;
    s->random_seed            = 91648253;
    s->progress_estimate      = 0;
    s->binary                 = (clause*)ABC_ALLOC( char, sizeof(clause) + sizeof(lit)*2);
    s->binary->size_learnt    = (2 << 1);
    s->verbosity              = 0;

    s->stats.starts           = 0;
    s->stats.decisions        = 0;
    s->stats.propagations     = 0;
    s->stats.inspects         = 0;
    s->stats.conflicts        = 0;
    s->stats.clauses          = 0;
    s->stats.clauses_literals = 0;
    s->stats.learnts          = 0;
    s->stats.learnts_literals = 0;
    s->stats.max_literals     = 0;
    s->stats.tot_literals     = 0;

#ifdef SAT_USE_SYSTEM_MEMORY_MANAGEMENT
    s->pMem = NULL;
#else
    s->pMem = Sat_MmStepStart( 10 );
#endif
    return s;
}


void sat_solver_delete(sat_solver* s)
{

#ifdef SAT_USE_SYSTEM_MEMORY_MANAGEMENT
    int i;
    for (i = 0; i < vecp_size(&s->clauses); i++)
        ABC_FREE(vecp_begin(&s->clauses)[i]);
    for (i = 0; i < vecp_size(&s->learnts); i++)
        ABC_FREE(vecp_begin(&s->learnts)[i]);
#else
    Sat_MmStepStop( s->pMem, 0 );
#endif

    // delete vectors
    vecp_delete(&s->clauses);
    vecp_delete(&s->learnts);
    veci_delete(&s->order);
    veci_delete(&s->trail_lim);
    veci_delete(&s->tagged);
    veci_delete(&s->stack);
    veci_delete(&s->model);
    veci_delete(&s->act_vars);
    veci_delete(&s->temp_clause);
    veci_delete(&s->conf_final);
    ABC_FREE(s->binary);

    // delete arrays
    if (s->wlists != 0){
        int i;
        for (i = 0; i < s->size*2; i++)
            vecp_delete(&s->wlists[i]);

        // if one is different from null, all are
        ABC_FREE(s->wlists   );
        ABC_FREE(s->activity );
        ABC_FREE(s->factors  );
        ABC_FREE(s->assigns  );
        ABC_FREE(s->orderpos );
        ABC_FREE(s->reasons  );
        ABC_FREE(s->levels   );
        ABC_FREE(s->trail    );
        ABC_FREE(s->tags     );
        ABC_FREE(s->polarity );
    }

    sat_solver_store_free(s);
    ABC_FREE(s);
}


int sat_solver_addclause(sat_solver* s, lit* begin, lit* end)
{
    lit *i,*j;
    int maxvar;
    lbool* values;
    lit last;

    veci_resize( &s->temp_clause, 0 );
    for ( i = begin; i < end; i++ )
        veci_push( &s->temp_clause, *i );
    begin = veci_begin( &s->temp_clause );
    end = begin + veci_size( &s->temp_clause );

    if (begin == end) 
        return false;

    //printlits(begin,end); printf("\n");
    // insertion sort
    maxvar = lit_var(*begin);
    for (i = begin + 1; i < end; i++){
        lit l = *i;
        maxvar = lit_var(l) > maxvar ? lit_var(l) : maxvar;
        for (j = i; j > begin && *(j-1) > l; j--)
            *j = *(j-1);
        *j = l;
    }
    sat_solver_setnvars(s,maxvar+1);
//    sat_solver_setnvars(s, lit_var(*(end-1))+1 );

    ///////////////////////////////////
    // add clause to internal storage
    if ( s->pStore )
    {
        int RetValue = Sto_ManAddClause( (Sto_Man_t *)s->pStore, begin, end );
        assert( RetValue );
    }
    ///////////////////////////////////

    //printlits(begin,end); printf("\n");
    values = s->assigns;

    // delete duplicates
    last = lit_Undef;
    for (i = j = begin; i < end; i++){
        //printf("lit: "L_LIT", value = %d\n", L_lit(*i), (lit_sign(*i) ? -values[lit_var(*i)] : values[lit_var(*i)]));
        lbool sig = !lit_sign(*i); sig += sig - 1;
        if (*i == lit_neg(last) || sig == values[lit_var(*i)])
            return true;   // tautology
        else if (*i != last && values[lit_var(*i)] == l_Undef)
            last = *j++ = *i;
    }

    //printf("final: "); printlits(begin,j); printf("\n");

    if (j == begin)          // empty clause
        return false;

    if (j - begin == 1) // unit clause
        return enqueue(s,*begin,(clause*)0);

    // create new clause
    vecp_push(&s->clauses,clause_new(s,begin,j,0));


    s->stats.clauses++;
    s->stats.clauses_literals += j - begin;

    return true;
}


int sat_solver_simplify(sat_solver* s)
{
    clause** reasons;
    int type;

    assert(sat_solver_dlevel(s) == 0);

    if (sat_solver_propagate(s) != 0)
        return false;

    if (s->qhead == s->simpdb_assigns || s->simpdb_props > 0)
        return true;

    reasons = s->reasons;
    for (type = 0; type < 2; type++){
        vecp*    cs  = type ? &s->learnts : &s->clauses;
        clause** cls = (clause**)vecp_begin(cs);

        int i, j;
        for (j = i = 0; i < vecp_size(cs); i++){
            if (reasons[lit_var(*clause_begin(cls[i]))] != cls[i] &&
                clause_simplify(s,cls[i]) == l_True)
                clause_remove(s,cls[i]);
            else
                cls[j++] = cls[i];
        }
        vecp_resize(cs,j);
    }

    s->simpdb_assigns = s->qhead;
    // (shouldn't depend on 'stats' really, but it will do for now)
    s->simpdb_props   = (int)(s->stats.clauses_literals + s->stats.learnts_literals);

    return true;
}

double luby(double y, int x)
{
    int size, seq;
    for (size = 1, seq = 0; size < x+1; seq++, size = 2*size + 1);
    while (size-1 != x){
        size = (size-1) >> 1;
        seq--;
        x = x % size;
    }
    return pow(y, (double)seq);
} 

void luby_test()
{
    int i;
    for ( i = 0; i < 20; i++ )
        printf( "%d ", (int)luby(2,i) );
    printf( "\n" );
}

int sat_solver_solve(sat_solver* s, lit* begin, lit* end, ABC_INT64_T nConfLimit, ABC_INT64_T nInsLimit, ABC_INT64_T nConfLimitGlobal, ABC_INT64_T nInsLimitGlobal)
{
    int restart_iter = 0;
    ABC_INT64_T  nof_conflicts;
    ABC_INT64_T  nof_learnts   = sat_solver_nclauses(s) / 3;
    lbool   status        = l_Undef;
    lbool*  values        = s->assigns;
    lit*    i;

    ////////////////////////////////////////////////
    if ( s->fSolved )
    {
        if ( s->pStore )
        {
            int RetValue = Sto_ManAddClause( (Sto_Man_t *)s->pStore, NULL, NULL );
            assert( RetValue );
        }
        return l_False;
    }
    ////////////////////////////////////////////////

    // set the external limits
    s->nCalls++;
    s->nRestarts  = 0;
    s->nConfLimit = 0;
    s->nInsLimit  = 0;
    if ( nConfLimit )
        s->nConfLimit = s->stats.conflicts + nConfLimit;
    if ( nInsLimit )
//        s->nInsLimit = s->stats.inspects + nInsLimit;
        s->nInsLimit = s->stats.propagations + nInsLimit;
    if ( nConfLimitGlobal && (s->nConfLimit == 0 || s->nConfLimit > nConfLimitGlobal) )
        s->nConfLimit = nConfLimitGlobal;
    if ( nInsLimitGlobal && (s->nInsLimit == 0 || s->nInsLimit > nInsLimitGlobal) )
        s->nInsLimit = nInsLimitGlobal;

#ifndef SAT_USE_ANALYZE_FINAL

    //printf("solve: "); printlits(begin, end); printf("\n");
    for (i = begin; i < end; i++){
        switch (lit_sign(*i) ? -values[lit_var(*i)] : values[lit_var(*i)]){
        case 1: // l_True: 
            break;
        case 0: // l_Undef
            assume(s, *i);
            if (sat_solver_propagate(s) == NULL)
                break;
            // fallthrough
        case -1: // l_False 
            sat_solver_canceluntil(s, 0);
            return l_False;
        }
    }
    s->root_level = sat_solver_dlevel(s);

#endif

/*
    // Perform assumptions:
    root_level = assumps.size();
    for (int i = 0; i < assumps.size(); i++){
        Lit p = assumps[i];
        assert(var(p) < nVars());
        if (!assume(p)){
            GClause r = reason[var(p)];
            if (r != GClause_NULL){
                Clause* confl;
                if (r.isLit()){
                    confl = propagate_tmpbin;
                    (*confl)[1] = ~p;
                    (*confl)[0] = r.lit();
                }else
                    confl = r.clause();
                analyzeFinal(confl, true);
                conflict.push(~p);
            }else
                conflict.clear(),
                conflict.push(~p);
            cancelUntil(0);
            return false; }
        Clause* confl = propagate();
        if (confl != NULL){
            analyzeFinal(confl), assert(conflict.size() > 0);
            cancelUntil(0);
            return false; }
    }
    assert(root_level == decisionLevel());
*/

#ifdef SAT_USE_ANALYZE_FINAL
    // Perform assumptions:
    s->root_level = end - begin;
    for ( i = begin; i < end; i++ )
    {
        lit p = *i;
        assert(lit_var(p) < s->size);
        veci_push(&s->trail_lim,s->qtail);
        if (!enqueue(s,p,(clause*)0))
        {
            clause * r = s->reasons[lit_var(p)];
            if (r != NULL)
            {
                clause* confl;
                if (clause_is_lit(r))
                {
                    confl = s->binary;
                    (clause_begin(confl))[1] = lit_neg(p);
                    (clause_begin(confl))[0] = clause_read_lit(r);
                }
                else
                    confl = r;
                sat_solver_analyze_final(s, confl, 1);
                veci_push(&s->conf_final, lit_neg(p));
            }
            else
            {
                veci_resize(&s->conf_final,0);
                veci_push(&s->conf_final, lit_neg(p));
            }
            sat_solver_canceluntil(s, 0);
            return l_False; 
        }
        else
        {
            clause* confl = sat_solver_propagate(s);
            if (confl != NULL){
                sat_solver_analyze_final(s, confl, 0);
                assert(s->conf_final.size > 0);
                sat_solver_canceluntil(s, 0);
                return l_False; }
        }
    }
    assert(s->root_level == sat_solver_dlevel(s));
#endif

    s->nCalls2++;

    if (s->verbosity >= 1){
        printf("==================================[MINISAT]===================================\n");
        printf("| Conflicts |     ORIGINAL     |              LEARNT              | Progress |\n");
        printf("|           | Clauses Literals |   Limit Clauses Literals  Lit/Cl |          |\n");
        printf("==============================================================================\n");
    }

    while (status == l_Undef){
//        int nConfs = 0;
        double Ratio = (s->stats.learnts == 0)? 0.0 :
            s->stats.learnts_literals / (double)s->stats.learnts;

        if (s->verbosity >= 1)
        {
            printf("| %9.0f | %7.0f %8.0f | %7.0f %7.0f %8.0f %7.1f | %6.3f %% |\n", 
                (double)s->stats.conflicts,
                (double)s->stats.clauses, 
                (double)s->stats.clauses_literals,
                (double)nof_learnts, 
                (double)s->stats.learnts, 
                (double)s->stats.learnts_literals,
                Ratio,
                s->progress_estimate*100);
            fflush(stdout);
        }
        nof_conflicts = (ABC_INT64_T)( 100 * luby(2, restart_iter++) );
//printf( "%d ", (int)nof_conflicts );
//        nConfs = s->stats.conflicts;
        status = sat_solver_search(s, nof_conflicts, nof_learnts);
//        if ( status == l_True )
//            printf( "%d ", s->stats.conflicts - nConfs );

//        nof_conflicts = nof_conflicts * 3 / 2; //*= 1.5;
        nof_learnts    = nof_learnts * 11 / 10; //*= 1.1;

        // quit the loop if reached an external limit
        if ( s->nConfLimit && s->stats.conflicts > s->nConfLimit )
        {
//            printf( "Reached the limit on the number of conflicts (%d).\n", s->nConfLimit );
            break;
        }
//        if ( s->nInsLimit  && s->stats.inspects > s->nInsLimit )
        if ( s->nInsLimit  && s->stats.propagations > s->nInsLimit )
        {
//            printf( "Reached the limit on the number of implications (%d).\n", s->nInsLimit );
            break;
        }
        if ( s->nRuntimeLimit && clock() > s->nRuntimeLimit )
            break;
    }
    if (s->verbosity >= 1)
        printf("==============================================================================\n");

    sat_solver_canceluntil(s,0);

    ////////////////////////////////////////////////
    if ( status == l_False && s->pStore )
    {
        int RetValue = Sto_ManAddClause( (Sto_Man_t *)s->pStore, NULL, NULL );
        assert( RetValue );
    }
    ////////////////////////////////////////////////
    return status;
}


int sat_solver_nvars(sat_solver* s)
{
    return s->size;
}


int sat_solver_nclauses(sat_solver* s)
{
    return vecp_size(&s->clauses);
}


int sat_solver_nconflicts(sat_solver* s)
{
    return (int)s->stats.conflicts;
}

//=================================================================================================
// Clause storage functions:

void sat_solver_store_alloc( sat_solver * s )
{
    assert( s->pStore == NULL );
    s->pStore = Sto_ManAlloc();
}

void sat_solver_store_write( sat_solver * s, char * pFileName )
{
    if ( s->pStore ) Sto_ManDumpClauses( (Sto_Man_t *)s->pStore, pFileName );
}

void sat_solver_store_free( sat_solver * s )
{
    if ( s->pStore ) Sto_ManFree( (Sto_Man_t *)s->pStore );
    s->pStore = NULL;
}

int sat_solver_store_change_last( sat_solver * s )
{
    if ( s->pStore ) return Sto_ManChangeLastClause( (Sto_Man_t *)s->pStore );
    return -1;
}
 
void sat_solver_store_mark_roots( sat_solver * s )
{
    if ( s->pStore ) Sto_ManMarkRoots( (Sto_Man_t *)s->pStore );
}

void sat_solver_store_mark_clauses_a( sat_solver * s )
{
    if ( s->pStore ) Sto_ManMarkClausesA( (Sto_Man_t *)s->pStore );
}

void * sat_solver_store_release( sat_solver * s )
{
    void * pTemp;
    if ( s->pStore == NULL )
        return NULL;
    pTemp = s->pStore;
    s->pStore = NULL;
    return pTemp;
}

//=================================================================================================
// Sorting functions (sigh):

static inline void selectionsort(void** array, int size, int(*comp)(const void *, const void *))
{
    int     i, j, best_i;
    void*   tmp;

    for (i = 0; i < size-1; i++){
        best_i = i;
        for (j = i+1; j < size; j++){
            if (comp(array[j], array[best_i]) < 0)
                best_i = j;
        }
        tmp = array[i]; array[i] = array[best_i]; array[best_i] = tmp;
    }
}


static void sortrnd(void** array, int size, int(*comp)(const void *, const void *), double* seed)
{
    if (size <= 15)
        selectionsort(array, size, comp);

    else{
        void*       pivot = array[irand(seed, size)];
        void*       tmp;
        int         i = -1;
        int         j = size;

        for(;;){
            do i++; while(comp(array[i], pivot)<0);
            do j--; while(comp(pivot, array[j])<0);

            if (i >= j) break;

            tmp = array[i]; array[i] = array[j]; array[j] = tmp;
        }

        sortrnd(array    , i     , comp, seed);
        sortrnd(&array[i], size-i, comp, seed);
    }
}

void sat_solver_sort(void** array, int size, int(*comp)(const void *, const void *))
{
//    int i;
    double seed = 91648253;
    sortrnd(array,size,comp,&seed);
//    for ( i = 1; i < size; i++ )
//        assert(comp(array[i-1], array[i])<0);
}
ABC_NAMESPACE_IMPL_END

