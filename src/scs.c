/* scs 1.0 */
#include "scs.h"
#include "normalize.h"

static idxint _lineLen_;
// constants and data structures
static const char* HEADER[] = {
	" Iter ", 
	" pri res ",
	" dua res ",
    " rel gap ",
	" pri obj ",
    " dua obj ",
    "  kappa  ",
	" time (s)",
};

static const idxint HEADER_LEN = 8;

static inline void updateDualVars(Data * d, Work * w);
static inline void projectCones(Data * d,Work * w,Cone * k, idxint iter);
static inline void sety(Data * d, Work * w, Sol * sol);
static inline void setx(Data * d, Work * w, Sol * sol);
static inline void sets(Data * d, Work * w, Sol * sol);
static inline void setSolution(Data * d, Work * w, Sol * sol, Info * info);
static inline void getInfo(Data * d, Work * w, Sol * sol, Info * info, struct residuals * r);
static inline void printSummary(Data * d,Work * w,idxint i, struct residuals *r);
static inline void printHeader(Data * d, Work * w, Cone * k);
static inline void printFooter(Data * d, Info * info, Work * w); 
static inline void freeWork(Work * w);
static inline void projectLinSys(Data * d,Work * w, idxint iter);
static inline Work * initWork(Data * d, Cone * k);
static inline idxint converged(Data * d, Work * w, struct residuals * r, idxint iter);
static inline idxint exactConverged(Data * d, Work * w, struct residuals * r, idxint iter);
static inline idxint validate(Data * d, Cone * k);
static inline void failureDefaultReturn(Data * d, Work * w, Cone * k, Sol * sol, Info * info);

idxint privateInitWork(Data * d, Work * w);
char * getLinSysSummary(Data * d, Info * info);
// solves [I A';A -I] x = b, stores result in b, s contains warm-start
void solveLinSys(Data * d, Work * w, pfloat * b, const pfloat * s, idxint iter);
void freePriv(Work * w);

#define PRINT_INTERVAL 100
#define CONVERGED_INTERVAL 20

/* scs returns one of the following integers: */
/* (zero should never be returned) */
#define FAILURE -4
#define INDETERMINATE -3
#define INFEASIBLE -2 // primal infeasible, dual unbounded
#define UNBOUNDED -1 // primal unbounded, dual infeasible
#define SOLVED 1

idxint scs(Data * d, Cone * k, Sol * sol, Info * info)
{
	if(d == NULL || k == NULL) {
		failureDefaultReturn(d, NULL, k, sol, info);
        return FAILURE;
	}
    tic();
	info->statusVal = 0; // not yet converged
    idxint i;
	struct residuals r = {-1, -1, -1, -1, -1, -1, -1};
    if (validate(d,k) < 0) {
        failureDefaultReturn(d, NULL, k, sol, info);
        return FAILURE;
    }
    Work * w = initWork(d,k);
	if (!w) {
        failureDefaultReturn(d, NULL, k, sol, info);
        return FAILURE;
    }
    if(d->VERBOSE) {
		printHeader(d, w, k);
	} 
    /* scs: */
	for (i=0; i < d->MAX_ITERS; ++i){
		memcpy(w->u_prev, w->u, w->l*sizeof(pfloat));
		
		projectLinSys(d,w,i);
		projectCones(d,w,k,i);
		updateDualVars(d,w);
	    
        info->statusVal = converged(d,w,&r,i);

		if (info->statusVal != 0) break;

		if (i % PRINT_INTERVAL == 0){
			if (d->VERBOSE) printSummary(d,w,i,&r);
		}
	}
	if(d->VERBOSE) printSummary(d,w,i,&r);
	setSolution(d,w,sol,info);

	if(d->NORMALIZE) unNormalize(d,w,sol);

    info->iter = i;
	
    getInfo(d,w,sol,info,&r);
	if(d->VERBOSE) printFooter(d, info, w);
	freeWork(w);
	return info->statusVal;
}

static inline idxint validate(Data * d, Cone * k) {
    idxint i;
    if (!d->Ax || !d->Ai || !d->Ap || !d->b || !d->c) {
        scs_printf("data incompletely specified\n");
        return -1;
    }
    if (d->m <= 0 || d->n <= 0) {
        scs_printf("m and n must both be greater than 0\n");
        return -1;
    }
    if (d->m < d->n) {
        scs_printf("m must be greater than or equal to n\n");
        return -1;
    }
    for (i = 0 ; i < d->n ; ++i) {
        if(d->Ap[i] >= d->Ap[i+1]) {
            scs_printf("Ap not strictly increasing\n");
            return -1;
        }
    }
    idxint rMax = 0;
    for (i = 0 ; i < d->Anz ; ++i) {
        if(d->Ai[i] > rMax) rMax = d->Ai[i];
    }
    if (rMax > d->m - 1) {
        scs_printf("number of rows in A inconsistent with input dimension\n");
        return -1;
    }
    if (d->Anz != d->Ap[d->n]) {
        scs_printf("inconsistent Anz and Ap[n]\n");
        return -1;
    }
    if (validateCones(k) < 0) {
        scs_printf("invalid cone dimensions\n");
        return -1;
    }
    if (getFullConeDims(k) != d->m) {
        scs_printf("cone dimensions not equal to num rows in A\n");
        return -1;
    }
    if (((float) d->Anz / d->m > d->n) || (d->Anz <= 0)) {
        scs_printf("Anz (nonzeros in A) = %i, outside of valid range\n", (int) d->Anz);
        return -1;
    }
    if (d->MAX_ITERS < 0) {
        scs_printf("MAX_ITERS must be positive\n");
        return -1;
    }
    if (d->EPS_ABS < 0) {
        scs_printf("EPS_ABS tolerance must be positive\n");
        return -1;
    }
    if (d->ALPH <= 0 || d->ALPH >= 2) {
        scs_printf("ALPH must be in (0,2)\n");
        return -1;
    }
    if (d->RHO_X < 0) {
        scs_printf("RHO_X must be positive (1e-3 works well).\n");
        return -1;
    }
    return 0;
}

static inline void failureDefaultReturn(Data * d, Work * w, Cone * k, Sol * sol, Info * info){
    info->relGap = NAN;
    info->resPri = NAN;
    info->resDual = NAN;
    info->pobj = NAN;
    info->dobj = NAN;
    info->iter = -1;
    info->statusVal = FAILURE;
    info->time = NAN;
    strcpy(info->status,"Failure");
    sol->x = scs_malloc(sizeof(pfloat)*d->n);
    scaleArray(sol->x,NAN,d->n);
    sol->y = scs_malloc(sizeof(pfloat)*d->m);
    scaleArray(sol->y,NAN,d->m);
    sol->s = scs_malloc(sizeof(pfloat)*d->m);
    scaleArray(sol->s,NAN,d->m);
    scs_printf("FAILURE\n");
    freeWork(w);
}

static inline idxint converged(Data * d, Work * w, struct residuals * r, idxint iter){
    /* approximate convergence check:
    pfloat tau = fabs(w->u[w->l-1]); // abs to prevent negative stopping tol
    pfloat kap = fabs(w->v[w->l-1]);
    r->resPri = calcNormDiff(w->u, w->u_t, w->l);
    r->resDual = calcNormDiff(w->u, w->u_prev, w->l);
    r->tau = tau;
    r->kap = kap;
    if (MIN(tau,kap)/MAX(tau,kap) < 1e-6 && MAX(r->resPri, r->resDual) < d->EPS_ABS*(tau+kap)){
        return 1;
    }
    */
    if (iter % CONVERGED_INTERVAL == 0) {
        return exactConverged(d,w,r,iter);
    }
    return 0;
}

static inline idxint exactConverged(Data * d, Work * w, struct residuals * r, idxint iter){
    pfloat * pr = scs_calloc(d->m,sizeof(pfloat));
    pfloat * dr = scs_calloc(d->n,sizeof(pfloat));
    pfloat * Axs = scs_calloc(d->m,sizeof(pfloat));
    pfloat * ATy = scs_calloc(d->n,sizeof(pfloat));

    pfloat tau = fabs(w->u[w->l-1]);
    pfloat kap = fabs(w->v[w->l-1]);
    pfloat * x = w->u, * y = &(w->u[d->n]);
    pfloat * D = w->D, * E = w->E;
    idxint i;

    /*
    // requires mult by A:
    pfloat * s = &(w->v[d->n]);
    accumByA(d,x,Axs); // Axs = Ax
    addScaledArray(Axs,s,d->m,1.0); // Axs = Ax + s
    memcpy(pr, Axs, d->m * sizeof(pfloat)); // pr = Ax + s
    addScaledArray(pr,d->b,d->m,-tau); // pr = Ax + s - b * tau
    */

    // does not require mult by A:
    memcpy(pr,&(w->u[d->n]),d->m * sizeof(pfloat));
    addScaledArray(pr,&(w->u_prev[d->n]),d->m,d->ALPH-2);
    addScaledArray(pr,&(w->u_t[d->n]),d->m,1-d->ALPH);
    addScaledArray(pr,d->b, d->m, w->u_t[w->l-1] - tau) ; // pr = Ax + s - b * tau
    memcpy(Axs, pr, d->m * sizeof(pfloat));
    addScaledArray(Axs, d->b, d->m, tau); // Axs = Ax + s

    pfloat cTx = innerProd(x,d->c,d->n);

    if (d->NORMALIZE) {
        kap /= (w->scale * w->sc_c * w->sc_b);
        for (i = 0; i < d->m; ++i) {
            pr[i] *= D[i]/(w->sc_b * w->scale);
            Axs[i] *= D[i]/(w->sc_b * w->scale);
        } 
        cTx /= (w->scale * w->sc_c * w->sc_b);
    }
    r->tau = tau;
    r->kap = kap;

    pfloat nmAxs = calcNorm(Axs,d->m);
    r->resPri = cTx < 0 ? w->nm_c * nmAxs / -cTx : NAN;
    //scs_printf("unbounded cert: %4e\n", w->nm_c * nmAxs / (1+w->nm_b) / -cTx);
    if (r->resPri < d->EPS_ABS) {
        return UNBOUNDED;
    }

    accumByAtrans(d,y,ATy); // ATy = A'y
    memcpy(dr, ATy, d->n * sizeof(pfloat));
    addScaledArray(dr,d->c,d->n,tau); // dr = A'y + c * tau    

    pfloat bTy = innerProd(y,d->b,d->m);

    if (d->NORMALIZE) {
        for (i = 0; i < d->n; ++i) {
            dr[i] *= E[i]/(w->sc_c * w->scale);
            ATy[i] *= E[i]/(w->sc_c * w->scale);
        }
        bTy /= (w->scale * w->sc_c * w->sc_b);
    }

    pfloat nmATy = calcNorm(ATy,d->n);
    r->resDual = bTy < 0 ? w->nm_b * nmATy / -bTy : NAN;
    //scs_printf("infeas cert: %4e\n", w->nm_b * nmATy / (1+w->nm_c) /  - bTy );
    if (r->resDual < d->EPS_ABS) {
        return INFEASIBLE;
    }
    r->relGap = NAN;

    idxint status = 0;
    if (tau > kap) {
        pfloat rpri = calcNorm(pr,d->m) / (1+w->nm_b) / tau;
        pfloat rdua = calcNorm(dr,d->n) / (1+w->nm_c) / tau;
        pfloat gap = fabs(cTx + bTy) / (tau + fabs(cTx) + fabs(bTy));

        r->resPri = rpri;
        r->resDual = rdua;
        r->relGap = gap;
        r->cTx = cTx / tau;
        r->bTy = bTy / tau;
        // scs_printf("primal resid: %4e, dual resid %4e, pobj %4e, dobj %4e, gap %4e\n", rpri,rdua,cTx,-bTy,gap);
        // scs_printf("primal resid: %4e, dual resid %4e, gap %4e\n",rpri,rdua,gap);
        if (MAX(MAX(rpri,rdua),gap) < d->EPS_ABS) {
            status = SOLVED;
        }
    } else {
        r->cTx = NAN;
        r->bTy = NAN;
    }
    scs_free(dr); scs_free(pr); scs_free(Axs); scs_free(ATy);
    return status;
}

static inline void getInfo(Data * d, Work * w, Sol * sol, Info * info, struct residuals * r){
    pfloat * x = sol->x, * y = sol->y, * s = sol->s;
    
    pfloat * dr = scs_calloc(d->n,sizeof(pfloat));
    pfloat * pr = scs_calloc(d->m,sizeof(pfloat));

    accumByA(d,x,pr); // pr = Ax
    addScaledArray(pr,s,d->m,1.0); // pr = Ax + s

    accumByAtrans(d,y,dr); // dr = A'y

    pfloat cTx = innerProd(x,d->c,d->n);
    pfloat bTy = innerProd(y,d->b,d->m);
    info->pobj = cTx;
    info->dobj = -bTy;
    if (info->statusVal == SOLVED){
        addScaledArray(pr,d->b,d->m,-1.0); // pr = Ax + s - b
        addScaledArray(dr,d->c,d->n,1.0); // dr = A'y + c
        info->relGap = fabs(cTx + bTy) / (1 + fabs(cTx) + fabs(bTy));
        info->resPri = calcNorm(pr,d->m) / (1 + w->nm_b);
        info->resDual = calcNorm(dr,d->n) / (1+ w->nm_c);
    } else {
        if (info->statusVal == UNBOUNDED) {    
            info->dobj = NAN;
            info->relGap = NAN;
            info->resPri = w->nm_c * calcNorm(pr,d->m) / -cTx ;
            info->resDual = NAN;
            scaleArray(x,-1/cTx,d->n);
            scaleArray(s,-1/cTx,d->m);
            info->pobj = -1;
        }
        else {
            info->pobj = NAN;
            info->relGap = NAN;
            info->resPri = NAN;
            info->resDual = w->nm_b * calcNorm(dr,d->n) / -bTy ;
            scaleArray(y,-1/bTy,d->m);
            info->dobj = -1;
        }
    }
    info->time = tocq();
    scs_free(dr); scs_free(pr);
}

static inline Work * initWork(Data *d, Cone * k) {

	Work * w = scs_malloc(sizeof(Work));
    
    w->nm_b = calcNorm(d->b, d->m);
    w->nm_c = calcNorm(d->c, d->n);

    //w->nm_b = calcNormInf(d->b, d->m);
    //w->nm_c = calcNormInf(d->c, d->n);
    //w->nm_Q = calcNormFroQ(d);

    if(d->NORMALIZE) {
		normalize(d,w,k);
	}
	else {
		w->D = NULL;
		w->E = NULL;
		w->sc_c = 1.0;
		w->sc_b = 1.0;
		w->scale = 1.0;
	}

	w->l = d->n+d->m+1;
	w->u = scs_calloc(w->l,sizeof(pfloat));
	w->u[w->l-1] = sqrt(w->l);
	w->v = scs_calloc(w->l,sizeof(pfloat));
	w->v[w->l-1] = sqrt(w->l);
	w->u_t = scs_calloc(w->l,sizeof(pfloat));
	w->u_prev = scs_calloc(w->l,sizeof(pfloat));
	w->h = scs_calloc((w->l-1),sizeof(pfloat));
	memcpy(w->h,d->c,d->n*sizeof(pfloat));
	memcpy(&(w->h[d->n]),d->b,d->m*sizeof(pfloat));
	w->g = scs_calloc((w->l-1),sizeof(pfloat));
	memcpy(w->g,w->h,(w->l-1)*sizeof(pfloat));
	/* initialize the private data: */
	idxint status = privateInitWork(d, w);
    if (status < 0){
		scs_printf("privateInitWork failure: %i\n",(int) status);
	    freeWork(w);
        return NULL;
    }
    status = initCone(k);
    if (status < 0){
        scs_printf("initCone failure: %i\n",(int) status);
        freeWork(w);
        return NULL;
    }
    solveLinSys(d,w,w->g, NULL, -1);
	
    scaleArray(&(w->g[d->n]),-1,d->m);
	w->gTh = innerProd(w->h, w->g, w->l-1); 
	return w;
}

static inline void projectLinSys(Data * d,Work * w, idxint iter){

	// ut = u + v
	memcpy(w->u_t,w->u,w->l*sizeof(pfloat));
	addScaledArray(w->u_t,w->v,w->l,1.0);
	
	scaleArray(w->u_t,d->RHO_X,d->n);

	addScaledArray(w->u_t,w->h,w->l-1,-w->u_t[w->l-1]);
	addScaledArray(w->u_t, w->h, w->l-1, -innerProd(w->u_t,w->g,w->l-1)/(w->gTh+1));
	scaleArray(&(w->u_t[d->n]),-1,d->m);
	
	solveLinSys(d, w, w->u_t, w->u, iter);
	
	w->u_t[w->l-1] += innerProd(w->u_t,w->h,w->l-1);
}

static inline void freeWork(Work * w){
	freePriv(w);
    finishCone();
	if(w){
        if(w->method) free(w->method); //called via malloc not mxMalloc
		if(w->u) scs_free(w->u);
		if(w->v) scs_free(w->v);
		if(w->u_t) scs_free(w->u_t);
		if(w->u_prev) scs_free(w->u_prev);
		if(w->h) scs_free(w->h);
		if(w->g) scs_free(w->g);
		if(w->D) scs_free(w->D);
		if(w->E) scs_free(w->E);
		scs_free(w);
	}
}

void printSol(Data * d, Sol * sol, Info * info){
	idxint i;
	scs_printf("%s\n",info->status); 
	if (sol->x != NULL){
		for ( i=0;i<d->n; ++i){
			scs_printf("x[%i] = %4f\n",(int) i, sol->x[i]);
		}
	}
	if (sol->y != NULL){
		for ( i=0;i<d->m; ++i){
			scs_printf("y[%i] = %4f\n", (int) i, sol->y[i]);
		}
	}
}

static inline void updateDualVars(Data * d, Work * w){
	idxint i;
	/*
	   for(i = 0; i < d->n; ++i) { 
	   w->v[i] += w->u[i] - w->u_t[i]; 
	   }
	 */
	//for(i = 0; i < w->l; ++i) { 
	if (fabs(d->ALPH - 1.0) < 1e-9) {
		// this is over-step parameter:
		//pfloat sig = (1+sqrt(5))/2;
		pfloat sig = 1.0;
		for(i = d->n; i < w->l; ++i) { 
			w->v[i] += sig*(w->u[i] - w->u_t[i]);
		}
	}
	else {
		// this does not relax 'x' variable
		for(i = d->n; i < w->l; ++i) { 
			w->v[i] += (w->u[i] - d->ALPH*w->u_t[i] - (1.0 - d->ALPH)*w->u_prev[i]); 
		}
	}
}

static inline void projectCones(Data *d,Work * w,Cone * k, idxint iter){
	idxint i;
	// this does not relax 'x' variable
	for(i = 0; i < d->n; ++i) { 
		w->u[i] = w->u_t[i] - w->v[i];
	}
	//for(i = 0; i < w->l; ++i){
	for(i = d->n; i < w->l; ++i){
		w->u[i] = d->ALPH*w->u_t[i] + (1-d->ALPH)*w->u_prev[i] - w->v[i];
	}
	/* u = [x;y;tau] */
	projCone(&(w->u[d->n]),k,w,iter);
	if (w->u[w->l-1]<0.0) w->u[w->l-1] = 0.0;
}

static inline idxint solved(Data * d, Sol * sol, Info * info, pfloat tau){
    strcpy(info->status,"Solved");
    scaleArray(sol->x,1.0/tau,d->n);
    scaleArray(sol->y,1.0/tau,d->m);
    scaleArray(sol->s,1.0/tau,d->m);
    return SOLVED;
}

static inline idxint indeterminate(Data * d, Sol * sol, Info * info){
    strcpy(info->status, "Indeterminate");
    scaleArray(sol->x,NAN,d->n);
    scaleArray(sol->y,NAN,d->m);
    scaleArray(sol->s,NAN,d->m);
    return INDETERMINATE;
}

static inline idxint infeasible(Data * d, Sol * sol, Info * info){
    strcpy(info->status,"Infeasible");
    //scaleArray(sol->y,-1/ip_y,d->m);
    scaleArray(sol->x,NAN,d->n);
    scaleArray(sol->s,NAN,d->m);
    return INFEASIBLE;
}

static inline idxint unbounded(Data * d, Sol * sol, Info * info){
    strcpy(info->status,"Unbounded");
    //scaleArray(sol->x,-1/ip_x,d->n);
    scaleArray(sol->y,NAN,d->m);
    return UNBOUNDED;
}

static inline void setSolution(Data * d, Work * w, Sol * sol, Info * info){
    setx(d,w,sol);
    sety(d,w,sol);
    sets(d,w,sol);
    if (info->statusVal == 0 || info->statusVal == SOLVED){
        pfloat tau = w->u[w->l-1];
        pfloat kap = fabs(w->v[w->l-1]);
        if (tau > d->UNDET_TOL && tau > kap){
            info->statusVal = solved(d,sol,info,tau);
        }   
        else{ 
            if (calcNorm(w->u,w->l)<d->UNDET_TOL*sqrt(w->l)){
                info->statusVal = indeterminate(d,sol,info);
            }   
            else {
                pfloat bTy = innerProd(d->b,sol->y,d->m);  
                pfloat cTx = innerProd(d->c,sol->x,d->n);  
                if (bTy < cTx){
                    info->statusVal = infeasible(d,sol,info);
                }   
                else{
                    info->statusVal = unbounded(d,sol,info);
                }
            }
        }
    } else if (info->statusVal == INFEASIBLE) {
        info->statusVal = infeasible(d,sol,info);
    } else {
        info->statusVal = unbounded(d,sol,info);
    }
}

static inline void sety(Data * d,Work * w, Sol * sol){
	sol->y = scs_malloc(sizeof(pfloat)*d->m);
	memcpy(sol->y, &(w->u[d->n]), d->m*sizeof(pfloat));
}

static inline void sets(Data * d,Work * w, Sol * sol){
    sol->s = scs_malloc(sizeof(pfloat)*d->m);
	memcpy(sol->s, &(w->v[d->n]), d->m*sizeof(pfloat));
}

static inline void setx(Data * d,Work * w, Sol * sol){
	sol->x = scs_malloc(sizeof(pfloat)*d->n);
	memcpy(sol->x, w->u, d->n*sizeof(pfloat));
}

static inline void printSummary(Data * d,Work * w,idxint i, struct residuals *r){
    scs_printf("%*i|", (int)strlen(HEADER[0]), (int) i);
	scs_printf(" %*.2e ", (int)strlen(HEADER[1])-1, r->resPri);
	scs_printf(" %*.2e ", (int)strlen(HEADER[2])-1, r->resDual);
	scs_printf(" %*.2e ", (int)strlen(HEADER[3])-1, r->relGap);
    if (r->cTx < 0) {
	    scs_printf("%*.2e ", (int)strlen(HEADER[4])-1, r->cTx);
    } else {
        scs_printf(" %*.2e ", (int)strlen(HEADER[4])-1, r->cTx);
    }
    if (r->bTy >= 0) {
	    scs_printf("%*.2e ", (int)strlen(HEADER[5])-1, -r->bTy);
    } else {
	    scs_printf(" %*.2e ", (int)strlen(HEADER[5])-1, -r->bTy);
    }
    scs_printf(" %*.2e ", (int)strlen(HEADER[6])-1, r->kap);
	scs_printf(" %*.2e ", (int)strlen(HEADER[7])-1, tocq()/1e3);
	scs_printf("\n");
#ifdef MATLAB_MEX_FILE
	mexEvalString("drawnow;");
#endif
}

static inline void printHeader(Data * d, Work * w, Cone * k) {
	idxint i;  
	_lineLen_ = -1;
    //scs_printf("size of idxint %lu, size of pfloat %lu\n", sizeof(idxint), sizeof(pfloat));
	for(i = 0; i < HEADER_LEN; ++i) {
		_lineLen_ += strlen(HEADER[i]) + 1;
	}
	for(i = 0; i < _lineLen_; ++i) {
		scs_printf("-");
	}
	scs_printf("\n\n\tscs v1.0 - (c) Brendan O'Donoghue, Stanford University, 2014\n\n");
       	for(i = 0; i < _lineLen_; ++i) {
		scs_printf("-");
	}
    scs_printf("\nmethod: %s\n", w->method);
    scs_printf("EPS = %.2e, ALPHA = %.2f, MAX_ITERS = %i, NORMALIZE = %i\n", d->EPS_ABS, d->ALPH, (int) d->MAX_ITERS, (int) d->NORMALIZE);
	scs_printf("variables n = %i, constraints m = %i, non-zeros in A = %li\n", (int) d->n, (int) d->m, (long) d->Anz);

    char * coneStr = getConeHeader(k);
    scs_printf("%s",coneStr);
    free(coneStr);

    for(i = 0; i < _lineLen_; ++i) {
		scs_printf("-");
	}
	scs_printf("\n");
	for(i = 0; i < HEADER_LEN - 1; ++i) {
		scs_printf("%s|", HEADER[i]);
	}
	scs_printf("%s\n", HEADER[HEADER_LEN-1]);
	for(i = 0; i < _lineLen_; ++i) {
		scs_printf("=");
	}
	scs_printf("\n");
}

static inline void printFooter(Data * d, Info * info, Work * w) {
	idxint i;
	for(i = 0; i < _lineLen_; ++i) {
		scs_printf("-");
	}
	scs_printf("\nStatus: %s\n",info->status);
	if (info->iter == d->MAX_ITERS) {
		scs_printf("Hit MAX_ITERS, solution may be inaccurate\n"); 
	}
    scs_printf("Time taken: %.4f seconds\n",info->time/1e3);

    char * linSysStr = getLinSysSummary(d, info);
    if (linSysStr) {
        scs_printf("%s",linSysStr);
        free(linSysStr);
    }

	for(i = 0; i < _lineLen_; ++i) {
		scs_printf("-");
	}
    scs_printf("\n");

    if (info->statusVal == INFEASIBLE) {
        scs_printf("Certificate of primal infeasibility:\n");
        scs_printf("|A'y|_2 * |b|_2 = %.4e\n", info->resDual);
        scs_printf("dist(y, K*) = 0\n");
        scs_printf("b'y = %.4f\n", info->dobj);
    } 
    else if (info->statusVal == UNBOUNDED) {
        scs_printf("Certificate of dual infeasibility:\n");
        scs_printf("|Ax + s|_2 * |c|_2 = %.4e\n", info->resPri);
        scs_printf("dist(s, K) = 0\n");
        scs_printf("c'x = %.4f\n", info->pobj);
    }
    else {
        scs_printf("Error metrics:\n");
        scs_printf("|Ax + s - b|_2 / (1 + |b|_2) = %.4e\n",info->resPri); 
        scs_printf("|A'y + c|_2 / (1 + |c|_2) = %.4e\n",info->resDual);
        scs_printf("|c'x + b'y| / (1 + |c'x| + |b'y|) = %.4e\n", info->relGap); 
        scs_printf("dist(s, K) = 0, dist(y, K*) = 0, s'y = 0\n");
        for(i = 0; i < _lineLen_; ++i) {
            scs_printf("-");
        }
        scs_printf("\n");
        scs_printf("c'x = %.4f, -b'y = %.4f\n",info->pobj, info->dobj);
    }
    for(i = 0; i < _lineLen_; ++i) {
        scs_printf("=");
    }
    scs_printf("\n");
}
