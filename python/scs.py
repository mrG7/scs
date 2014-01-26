#!/usr/bin/env python
import _scs
from warnings import warn
from scipy import sparse


def solve(probdata, cone, opts):
    """ This Python routine "unpacks" scipy sparse matrix A into the
        data structures that we need for calling scs routine.
        
        It is *not* compatible with CVXOPT spmatrix and matrix, although
        it would not be very difficult to make it compatible. We put the
        onus on the user to convert CVXOPT matrix types into numpy, scipy
        array types.
    """
    if not 'A' in probdata or not 'b' in probdata or not 'c' in probdata:
        raise TypeError("Missing A, b, c from data dictionary")
    A = probdata['A']
    b = probdata['b']
    c = probdata['c']

    if A is None or b is None or c is None:
        raise TypeError("Incomplete data specification")
    if not sparse.issparse(A):
        raise TypeError("A is required to be a sparse matrix")
    if not sparse.isspmatrix_csc(A):
        warn("Converting A to a CSC (compressed sparse column) matrix; may take a while.")
        A = A.tocsc()

    if sparse.issparse(b):
        b = b.toDense()

    if sparse.issparse(c):
        c = c.toDense()

    m, n = A.shape

    Adata, Aindices, Acolptr = A.data, A.indices, A.indptr

    return _scs.csolve((m, n), Adata, Aindices, Acolptr, b, c, cone, opts)
