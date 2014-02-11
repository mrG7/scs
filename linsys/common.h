#ifndef COMMON_H_GUARD
#define COMMON_H_GUARD

#include "amatrix.h"

/* contains routines common to direct and indirect sparse solvers */

#define MIN_SCALE 1e-2
#define MAX_SCALE 1e3

idxint validateLinSys(Data *d) {
	AMatrix * A = d->A;
	idxint i, rMax, Anz;
	if (!A->x || !A->i || !A->p) {
		scs_printf("data incompletely specified\n");
		return -1;
	}
	for (i = 0; i < d->n; ++i) {
		if (A->p[i] >= A->p[i + 1]) {
			scs_printf("A->p not strictly increasing\n");
			return -1;
		}
	}
	Anz = A->p[d->n];
	if (((pfloat) Anz / d->m > d->n) || (Anz <= 0)) {
		scs_printf("Anz (nonzeros in A) = %i, outside of valid range\n", (int) Anz);
		return -1;
	}
	rMax = 0;
	for (i = 0; i < Anz; ++i) {
		if (A->i[i] > rMax)
			rMax = A->i[i];
	}
	if (rMax > d->m - 1) {
		scs_printf("number of rows in A inconsistent with input dimension\n");
		return -1;
	}
	return 0;
}

void normalizeA(Data * d, Work * w, Cone * k) {
	AMatrix * A = d->A;
	pfloat * D = scs_calloc(d->m, sizeof(pfloat));
	pfloat * E = scs_calloc(d->n, sizeof(pfloat));

	idxint i, j, count;
	pfloat wrk, *nms;

	/* calculate row norms */
	for (i = 0; i < d->n; ++i) {
		for (j = A->p[i]; j < A->p[i + 1]; ++j) {
			wrk = A->x[j];
			D[A->i[j]] += wrk * wrk;
		}
	}
	for (i = 0; i < d->m; ++i) {
		D[i] = sqrt(D[i]); /* just the norms */
	}
	/* mean of norms of rows across each cone  */
	count = k->l + k->f;
	for (i = 0; i < k->qsize; ++i) {
		wrk = 0;
		/*
		 for (j = count; j < count + k->q[i]; ++j){
		 wrk = MAX(wrk,D[j]);
		 }
		 */
		for (j = count; j < count + k->q[i]; ++j) {
			wrk += D[j];
		}
		wrk /= k->q[i];
		for (j = count; j < count + k->q[i]; ++j) {
			D[j] = wrk;
		}
		count += k->q[i];
	}
	for (i = 0; i < k->ssize; ++i) {
		wrk = 0;
		/*
		 for (j = count; j < count + (k->s[i])*(k->s[i]); ++j){
		 wrk = MAX(wrk,D[j]);
		 }
		 */
		for (j = count; j < count + (k->s[i]) * (k->s[i]); ++j) {
			wrk += D[j] * D[j];
		}
		wrk = sqrt(wrk);
		wrk /= k->s[i];
		for (j = count; j < count + (k->s[i]) * (k->s[i]); ++j) {
			D[j] = wrk;
		}
		count += (k->s[i]) * (k->s[i]);
	}

	for (i = 0; i < k->ep + k->ed; ++i) {
		wrk = D[count] / 3 + D[count + 1] / 3 + D[count + 2] / 3;
		D[count] = wrk;
		D[count + 1] = wrk;
		D[count + 2] = wrk;
		count += 3;
	}

	for (i = 0; i < d->m; ++i) {
		if (D[i] < MIN_SCALE)
			D[i] = 1;
		else if (D[i] > MAX_SCALE)
			D[i] = MAX_SCALE;

	}
	/* scale the rows with D */
	for (i = 0; i < d->n; ++i) {
		for (j = A->p[i]; j < A->p[i + 1]; ++j) {
			A->x[j] /= D[A->i[j]];
		}
	}
	/* calculate and scale by col norms, E */
	for (i = 0; i < d->n; ++i) {
		E[i] = calcNorm(&(A->x[A->p[i]]), A->p[i + 1] - A->p[i]);
		if (E[i] < MIN_SCALE)
			E[i] = 1;
		else if (E[i] > MAX_SCALE)
			E[i] = MAX_SCALE;
		scaleArray(&(A->x[A->p[i]]), 1.0 / E[i], A->p[i + 1] - A->p[i]);
	}

	nms = scs_calloc(d->m, sizeof(pfloat));
	for (i = 0; i < d->n; ++i) {
		for (j = A->p[i]; j < A->p[i + 1]; ++j) {
			wrk = A->x[j];
			nms[A->i[j]] += wrk * wrk;
		}
	}
	w->meanNormRowA = 0.0;
	for (i = 0; i < d->m; ++i) {
		w->meanNormRowA += sqrt(nms[i]) / d->m;
	}
	scs_free(nms);

	if (d->SCALE != 1) {
		scaleArray(A->x, d->SCALE, A->p[d->n]);
	}

	w->D = D;
	w->E = E;
}

void unNormalizeA(Data *d, Work * w) {
	idxint i, j;
	pfloat * D = w->D;
	pfloat * E = w->E;
	AMatrix * A = d->A;
	for (i = 0; i < d->n; ++i) {
		scaleArray(&(A->x[A->p[i]]), E[i] / d->SCALE, A->p[i + 1] - A->p[i]);
	}
	for (i = 0; i < d->n; ++i) {
		for (j = A->p[i]; j < A->p[i + 1]; ++j) {
			A->x[j] *= D[A->i[j]];
		}
	}
}

#endif