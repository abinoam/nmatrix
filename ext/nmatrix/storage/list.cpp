/////////////////////////////////////////////////////////////////////
// = NMatrix
//
// A linear algebra library for scientific computation in Ruby.
// NMatrix is part of SciRuby.
//
// NMatrix was originally inspired by and derived from NArray, by
// Masahiro Tanaka: http://narray.rubyforge.org
//
// == Copyright Information
//
// SciRuby is Copyright (c) 2010 - 2012, Ruby Science Foundation
// NMatrix is Copyright (c) 2012, Ruby Science Foundation
//
// Please see LICENSE.txt for additional copyright notices.
//
// == Contributing
//
// By contributing source code to SciRuby, you agree to be bound by
// our Contributor Agreement:
//
// * https://github.com/SciRuby/sciruby/wiki/Contributor-Agreement
//
// == list.c
//
// List-of-lists n-dimensional matrix storage. Uses singly-linked
// lists.

/*
 * Standard Includes
 */

#include <ruby.h>

/*
 * Project Includes
 */

#include "types.h"

#include "data/data.h"

#include "common.h"
#include "list.h"

#include "util/math.h"
#include "util/sl_list.h"

/*
 * Macros
 */

/*
 * Global Variables
 */

/*
 * Forward Declarations
 */

template <typename LDType, typename RDType>
static LIST_STORAGE* list_storage_cast_copy_template(const LIST_STORAGE* rhs, dtype_t new_dtype);

template <typename LDType, typename RDType>
static bool list_storage_eqeq_template(const LIST_STORAGE* left, const LIST_STORAGE* right);

template <ewop_t op, typename LDType, typename RDType>
static void* list_storage_ew_op_template(LIST* dest, const LIST* left, const void* l_default, const LIST* right, const void* r_default, const size_t* shape, size_t rank);

template <ewop_t op, typename LDType, typename RDType>
static void list_storage_ew_op_template_prime(LIST* dest, LDType d_default, const LIST* left, LDType l_default, const LIST* right, RDType r_default, const size_t* shape, size_t last_level, size_t level);

/*
 * Functions
 */

////////////////
// Lifecycle //
///////////////

/*
 * Creates a list-of-lists(-of-lists-of-lists-etc) storage framework for a
 * matrix.
 *
 * Note: The pointers you pass in for shape and init_val become property of our
 * new storage. You don't need to free them, and you shouldn't re-use them.
 */
LIST_STORAGE* list_storage_create(dtype_t dtype, size_t* shape, size_t rank, void* init_val) {
  LIST_STORAGE* s;

  s = ALLOC( LIST_STORAGE );

  s->rank  = rank;
  s->shape = shape;
  s->dtype = dtype;

  s->rows  = list_create();

  s->default_val = init_val;

  return s;
}

/*
 * Documentation goes here.
 */
void list_storage_delete(STORAGE* s) {
  if (s) {
    LIST_STORAGE* storage = (LIST_STORAGE*)s;

    list_delete( storage->rows, storage->rank - 1 );

    free(storage->shape);
    free(storage->default_val);
    free(s);
  }
}

/*
 * Documentation goes here.
 */
void list_storage_mark(void* storage_base) {
  LIST_STORAGE* storage = (LIST_STORAGE*)storage_base;

  if (storage && storage->dtype == RUBYOBJ) {
    rb_gc_mark(*((VALUE*)(storage->default_val)));
    list_mark(storage->rows, storage->rank - 1);
  }
}

///////////////
// Accessors //
///////////////


/*
 * Documentation goes here.
 */
void* list_storage_get(STORAGE* storage, SLICE* slice) {
  //LIST_STORAGE* s = (LIST_STORAGE*)storage;
  rb_raise(rb_eNotImpError, "This type of slicing not supported yet");
}


/*
 * Get the contents of some set of coordinates. Note: Does not make a copy!
 * Don't free!
 */
void* list_storage_ref(STORAGE* storage, SLICE* slice) {
  LIST_STORAGE* s = (LIST_STORAGE*)storage;
  size_t r;
  NODE*  n;
  LIST*  l = s->rows;

  for (r = s->rank; r > 1; --r) {
    n = list_find(l, slice->coords[s->rank - r]);
    if (n)  l = reinterpret_cast<LIST*>(n->val);
    else return s->default_val;
  }

  n = list_find(l, slice->coords[s->rank - r]);
  if (n) return n->val;
  else   return s->default_val;
}

/*
 * Documentation goes here.
 *
 * TODO: Allow this function to accept an entire row and not just one value -- for slicing
 */
void* list_storage_insert(STORAGE* storage, SLICE* slice, void* val) {
  LIST_STORAGE* s = (LIST_STORAGE*)storage;
  // Pretend ranks = 2
  // Then coords is going to be size 2
  // So we need to find out if some key already exists
  size_t r;
  NODE*  n;
  LIST*  l = s->rows;

  // drill down into the structure
  for (r = s->rank; r > 1; --r) {
    n = list_insert(l, false, slice->coords[s->rank - r], list_create());
    l = reinterpret_cast<LIST*>(n->val);
  }

  n = list_insert(l, true, slice->coords[s->rank - r], val);
  return n->val;
}

/*
 * Documentation goes here.
 *
 * TODO: Speed up removal.
 */
void* list_storage_remove(STORAGE* storage, SLICE* slice) {
  LIST_STORAGE* s = (LIST_STORAGE*)storage;
  int r;
  NODE  *n = NULL;
  LIST*  l = s->rows;
  void*  rm = NULL;

  // keep track of where we are in the traversals
  NODE** stack = ALLOCA_N( NODE*, s->rank - 1 );

  for (r = (int)(s->rank); r > 1; --r) {
  	// does this row exist in the matrix?
    n = list_find(l, slice->coords[s->rank - r]);

    if (!n) {
    	// not found
      free(stack);
      return NULL;
      
    } else {
    	// found
      stack[s->rank - r]    = n;
      l                     = reinterpret_cast<LIST*>(n->val);
    }
  }

  rm = list_remove(l, slice->coords[s->rank - r]);

  // if we removed something, we may now need to remove parent lists
  if (rm) {
    for (r = (int)(s->rank) - 2; r >= 0; --r) {
    	// walk back down the stack
      
      if (((LIST*)(stack[r]->val))->first == NULL)
        free(list_remove(reinterpret_cast<LIST*>(stack[r]->val), slice->coords[r]));
      else break; // no need to continue unless we just deleted one.

    }
  }

  return rm;
}

///////////
// Tests //
///////////

bool list_storage_eqeq(const STORAGE* left, const STORAGE* right) {
	NAMED_LR_DTYPE_TEMPLATE_TABLE(ttable, list_storage_eqeq_template, bool, const LIST_STORAGE* left, const LIST_STORAGE* right);

	return ttable[left->dtype][right->dtype]((const LIST_STORAGE*)left, (const LIST_STORAGE*)right);
}

//////////
// Math //
//////////

/*
 * Documentation goes here.
 */
STORAGE* list_storage_ew_op(ewop_t op, const STORAGE* left, const STORAGE* right) {
	OP_LR_DTYPE_TEMPLATE_TABLE(list_storage_ew_op_template, void*, LIST*, const LIST*, const void*, const LIST*, const void*, const size_t*, size_t);
	
	dtype_t new_dtype = Upcast[left->dtype][right->dtype];
	
	const LIST_STORAGE* l = reinterpret_cast<const LIST_STORAGE*>(left),
										* r = reinterpret_cast<const LIST_STORAGE*>(right);
	
	LIST_STORAGE* new_l = NULL;
	
	// Allocate a new shape array for the resulting matrix.
	size_t* new_shape = (size_t*)calloc(l->rank, sizeof(size_t));
	memcpy(new_shape, left->shape, sizeof(size_t) * l->rank);
	
	// Create the result matrix.
	LIST_STORAGE* result = list_storage_create(new_dtype, new_shape, left->rank, NULL); 
	
	/*
	 * Call the templated elementwise multiplication function and set the default
	 * value for the resulting matrix.
	 */
	if (new_dtype != left->dtype) {
		// Upcast the left-hand side if necessary.
		new_l = reinterpret_cast<LIST_STORAGE*>(list_storage_cast_copy(l, new_dtype));
		
		result->default_val =
			ttable[op][left->dtype][right->dtype](result->rows, new_l->rows, new_l->default_val, r->rows, r->default_val, result->shape, result->rank);
		
		// Delete the temporary left-hand side matrix.
		list_storage_delete(reinterpret_cast<STORAGE*>(new_l));
			
	} else {
		result->default_val =
			ttable[op][left->dtype][right->dtype](result->rows, l->rows, l->default_val, r->rows, r->default_val, result->shape, result->rank);
	}
	
	return result;
}


/*
 * Documentation goes here.
 */
STORAGE* list_storage_matrix_multiply(const STORAGE_PAIR& casted_storage, size_t* resulting_shape, bool vector) {
  free(resulting_shape);
  rb_raise(rb_eNotImpError, "multiplication not implemented for list-of-list matrices");
  return NULL;
  //DTYPE_TEMPLATE_TABLE(dense_storage_matrix_multiply_template, NMATRIX*, STORAGE_PAIR, size_t*, bool);

  //return ttable[reinterpret_cast<DENSE_STORAGE*>(casted_storage.left)->dtype](casted_storage, resulting_shape, vector);
}

/////////////
// Utility //
/////////////

/*
 * Documentation goes here.
 */
size_t list_storage_count_elements_r(const LIST* l, size_t recursions) {
  size_t count = 0;
  NODE* curr = l->first;
  
  if (recursions) {
    while (curr) {
      count += list_storage_count_elements_r(reinterpret_cast<const LIST*>(curr->val), recursions - 1);
      curr   = curr->next;
    }
    
  } else {
    while (curr) {
      ++count;
      curr = curr->next;
    }
  }
  
  return count;
}

/*
 * Count non-diagonal non-zero elements.
 */
size_t list_storage_count_nd_elements(const LIST_STORAGE* s) {
  NODE *i_curr, *j_curr;
  size_t count = 0;
  
  if (s->rank != 2) {
  	rb_raise(rb_eNotImpError, "non-diagonal element counting only defined for rank = 2");
  }

  for (i_curr = s->rows->first; i_curr; i_curr = i_curr->next) {
    for (j_curr = ((LIST*)(i_curr->val))->first; j_curr; j_curr = j_curr->next) {
      if (i_curr->key != j_curr->key) {
      	++count;
      }
    }
  }
  
  return count;
}

/////////////////////////
// Copying and Casting //
/////////////////////////

/*
 * List storage copy constructor C access.
 */
STORAGE* list_storage_cast_copy(const STORAGE* rhs, dtype_t new_dtype) {
  NAMED_LR_DTYPE_TEMPLATE_TABLE(ttable, list_storage_cast_copy_template, LIST_STORAGE*, const LIST_STORAGE* rhs, dtype_t new_dtype);

  return (STORAGE*)ttable[new_dtype][rhs->dtype]((LIST_STORAGE*)rhs, new_dtype);
}

/*
 * Documentation goes here.
 */
LIST_STORAGE* list_storage_copy(LIST_STORAGE* rhs) {
  LIST_STORAGE* lhs;
  size_t* shape;
  void* default_val = ALLOC_N(char, DTYPE_SIZES[rhs->dtype]);

  //fprintf(stderr, "copy_list_storage\n");

  // allocate and copy shape
  shape = ALLOC_N(size_t, rhs->rank);
  memcpy(shape, rhs->shape, rhs->rank * sizeof(size_t));
  memcpy(default_val, rhs->default_val, DTYPE_SIZES[rhs->dtype]);

  lhs = list_storage_create(rhs->dtype, shape, rhs->rank, default_val);

  if (lhs) {
    lhs->rows = list_create();
    list_cast_copy_contents(lhs->rows, rhs->rows, rhs->dtype, rhs->dtype, rhs->rank - 1);
  } else {
  	free(shape);
  }

  return lhs;
}

/////////////////////////
// Templated Functions //
/////////////////////////

/*
 * List storage copy constructor for changing dtypes.
 */
template <typename LDType, typename RDType>
static LIST_STORAGE* list_storage_cast_copy_template(const LIST_STORAGE* rhs, dtype_t new_dtype) {

  // allocate and copy shape
  size_t* shape = ALLOC_N(size_t, rhs->rank);
  memcpy(shape, rhs->shape, rhs->rank * sizeof(size_t));

  // copy default value
  LDType* default_val = ALLOC_N(LDType, 1);
  *default_val = *reinterpret_cast<RDType*>(rhs->default_val);

  LIST_STORAGE* lhs = list_storage_create(new_dtype, shape, rhs->rank, default_val);
  lhs->rows         = list_create();
  list_cast_copy_contents_template<LDType, RDType>(lhs->rows, rhs->rows, rhs->rank - 1);

  return lhs;
}


/*
 * List storage copy constructor for transposing.
 */
STORAGE* list_storage_copy_transposed(const STORAGE* rhs_base) {
  rb_raise(rb_eNotImpError, "list storage transpose not yet implemented");
  return NULL;
}


/*
 * Do these two dense matrices of the same dtype have exactly the same
 * contents?
 */
template <typename LDType, typename RDType>
bool list_storage_eqeq_template(const LIST_STORAGE* left, const LIST_STORAGE* right) {

  // in certain cases, we need to keep track of the number of elements checked.
  size_t num_checked  = 0,

	max_elements = storage_count_max_elements(left);

  if (!left->rows->first) {
    // Easy: both lists empty -- just compare default values
    if (!right->rows->first) {
    	return *reinterpret_cast<LDType*>(left->default_val) == *reinterpret_cast<RDType*>(right->default_val);
    	
    } else if (!list_eqeq_value_template<RDType,LDType>(right->rows, reinterpret_cast<LDType*>(left->default_val), left->rank-1, num_checked)) {
    	// Left empty, right not empty. Do all values in right == left->default_val?
    	return false;
    	
    } else if (num_checked < max_elements) {
    	// If the matrix isn't full, we also need to compare default values.
    	return *reinterpret_cast<LDType*>(left->default_val) == *reinterpret_cast<RDType*>(right->default_val);
    }

  } else if (!right->rows->first) {
    // fprintf(stderr, "!right->rows true\n");
    // Right empty, left not empty. Do all values in left == right->default_val?
    if (!list_eqeq_value_template<LDType,RDType>(left->rows, reinterpret_cast<RDType*>(right->default_val), left->rank-1, num_checked)) {
    	return false;
    	
    } else if (num_checked < max_elements) {
   		// If the matrix isn't full, we also need to compare default values.
    	return *reinterpret_cast<LDType*>(left->default_val) == *reinterpret_cast<RDType*>(right->default_val);
    }

  } else {
    // fprintf(stderr, "both matrices have entries\n");
    // Hardest case. Compare lists node by node. Let's make it simpler by requiring that both have the same default value
    if (!list_eqeq_list_template<LDType,RDType>(left->rows, right->rows, reinterpret_cast<LDType*>(left->default_val), reinterpret_cast<RDType*>(right->default_val), left->rank-1, num_checked)) {
    	return false;
    	
    } else if (num_checked < max_elements) {
      return *reinterpret_cast<LDType*>(left->default_val) == *reinterpret_cast<RDType*>(right->default_val);
    }
  }

  return true;
}

/*
 * Documentation goes here.
 */
template <ewop_t op, typename LDType, typename RDType>
static void* list_storage_ew_op_template(LIST* dest, const LIST* left, const void* l_default, const LIST* right, const void* r_default, const size_t* shape, size_t rank) {
	
	/*
	 * Allocate space for, and calculate, the default value for the destination
	 * matrix.
	 */
	LDType* d_default_mem = ALLOC(LDType);
	
	switch (op) {
		case EW_ADD:
			*d_default_mem = *reinterpret_cast<const LDType*>(l_default) + *reinterpret_cast<const RDType*>(r_default);
			break;
			
		case EW_SUB:
			*d_default_mem = *reinterpret_cast<const LDType*>(l_default) - *reinterpret_cast<const RDType*>(r_default);
			break;
			
		case EW_MUL:
			*d_default_mem = *reinterpret_cast<const LDType*>(l_default) * *reinterpret_cast<const RDType*>(r_default);
			break;
			
		case EW_DIV:
			*d_default_mem = *reinterpret_cast<const LDType*>(l_default) / *reinterpret_cast<const RDType*>(r_default);
			break;
			
		case EW_MOD:
			rb_raise(rb_eNotImpError, "Element-wise modulo is currently not supported.");
			break;
	}
	
	// Now that setup is done call the actual elementwise multiplication function.
	list_storage_ew_op_template_prime<op, LDType, RDType>(dest, *reinterpret_cast<const LDType*>(d_default_mem),
		left, *reinterpret_cast<const LDType*>(l_default), right, *reinterpret_cast<const RDType*>(r_default), shape, rank - 1, 0);
	
	// Return a pointer to the destination matrix's default value.
	return d_default_mem;
}

/*
 * Documentation goes here.
 */
template <ewop_t op, typename LDType, typename RDType>
static void list_storage_ew_op_template_prime(LIST* dest, LDType d_default, const LIST* left, LDType l_default, const LIST* right, RDType r_default, const size_t* shape, size_t last_level, size_t level) {
	
	static LIST EMPTY_LIST = {NULL};
	
	size_t index;
	
	LDType tmp_result;
	
	LIST* new_level = NULL;
	
	NODE* l_node		= left->first,
			* r_node		= right->first,
			* dest_node	= NULL;
	
	for (index = 0; index < shape[level]; ++index) {
		if (l_node == NULL and r_node == NULL) {
			/*
			 * Both source lists are now empty.  Because the default value of the
			 * destination is already set appropriately we can now return.
			 */
			
			return;
			
		} else {
			// At least one list still has entries.
			
			if (l_node == NULL and (l_default == 0 and d_default == 0)) {
				/* 
				 * The left hand list has run out of elements.  We don't need to add new
				 * values to the destination if l_default and d_default are both 0.
				 */
				
				return;
			
			} else if (r_node == NULL and (r_default == 0 and d_default == 0)) {
				/*
				 * The right hand list has run out of elements.  We don't need to add new
				 * values to the destination if r_default and d_default are both 0.
				 */
				
				return;
			}
			
			// We need to continue processing the lists.
			
			if (l_node == NULL and r_node->key == index) {
				/*
				 * One source list is empty, but the index has caught up to the key of
				 * the other list.
				 */
				
				if (level == last_level) {
					switch (op) {
						case EW_ADD:
							tmp_result = l_default + *reinterpret_cast<RDType*>(r_node->val);
							break;
							
						case EW_SUB:
							tmp_result = l_default - *reinterpret_cast<RDType*>(r_node->val);
							break;
							
						case EW_MUL:
							tmp_result = l_default * *reinterpret_cast<RDType*>(r_node->val);
							break;
							
						case EW_DIV:
							tmp_result = l_default / *reinterpret_cast<RDType*>(r_node->val);
							break;
							
						case EW_MOD:
							rb_raise(rb_eNotImpError, "Element-wise modulo is currently not supported.");
							break;
					}
					
					if (tmp_result != d_default) {
						dest_node = list_insert_helper(dest, dest_node, index, tmp_result);
					}
					
				} else {
					new_level = list_create();
					dest_node = list_insert_helper(dest, dest_node, index, new_level);
				
					list_storage_ew_op_template_prime<op, LDType, RDType>(new_level, d_default,
						&EMPTY_LIST, l_default,
						reinterpret_cast<LIST*>(r_node->val), r_default,
						shape, last_level, level + 1);
				}
				
				r_node = r_node->next;
				
			} else if (r_node == NULL and l_node->key == index) {
				/*
				 * One source list is empty, but the index has caught up to the key of
				 * the other list.
				 */
				
				if (level == last_level) {
					switch (op) {
						case EW_ADD:
							tmp_result = *reinterpret_cast<LDType*>(l_node->val) + r_default;
							break;
							
						case EW_SUB:
							tmp_result = *reinterpret_cast<LDType*>(l_node->val) - r_default;
							break;
							
						case EW_MUL:
							tmp_result = *reinterpret_cast<LDType*>(l_node->val) * r_default;
							break;
							
						case EW_DIV:
							tmp_result = *reinterpret_cast<LDType*>(l_node->val) / r_default;
							break;
							
						case EW_MOD:
							rb_raise(rb_eNotImpError, "Element-wise modulo is currently not supported.");
							break;
					}
					
					if (tmp_result != d_default) {
						dest_node = list_insert_helper(dest, dest_node, index, tmp_result);
					}
					
				} else {
					new_level = list_create();
					dest_node = list_insert_helper(dest, dest_node, index, new_level);
				
					list_storage_ew_op_template_prime<op, LDType, RDType>(new_level, d_default,
						reinterpret_cast<LIST*>(r_node->val), l_default,
						&EMPTY_LIST, r_default,
						shape, last_level, level + 1);
				}
				
				l_node = l_node->next;
				
			} else if (l_node != NULL and r_node != NULL and index == NM_MIN(l_node->key, r_node->key)) {
				/*
				 * Neither list is empty and our index has caught up to one of the
				 * source lists.
				 */
				
				if (l_node->key == r_node->key) {
					
					if (level == last_level) {
						switch (op) {
							case EW_ADD:
								tmp_result = *reinterpret_cast<LDType*>(l_node->val) + *reinterpret_cast<RDType*>(r_node->val);
								break;
							
							case EW_SUB:
								tmp_result = *reinterpret_cast<LDType*>(l_node->val) - *reinterpret_cast<RDType*>(r_node->val);
								break;
							
							case EW_MUL:
								tmp_result = *reinterpret_cast<LDType*>(l_node->val) * *reinterpret_cast<RDType*>(r_node->val);
								break;
							
							case EW_DIV:
								tmp_result = *reinterpret_cast<LDType*>(l_node->val) / *reinterpret_cast<RDType*>(r_node->val);
								break;
							
							case EW_MOD:
								rb_raise(rb_eNotImpError, "Element-wise modulo is currently not supported.");
								break;
						}
						
						if (tmp_result != d_default) {
							dest_node = list_insert_helper(dest, dest_node, index, tmp_result);
						}
						
					} else {
						new_level = list_create();
						dest_node = list_insert_helper(dest, dest_node, index, new_level);
					
						list_storage_ew_op_template_prime<op, LDType, RDType>(new_level, d_default,
							reinterpret_cast<LIST*>(l_node->val), l_default,
							reinterpret_cast<LIST*>(r_node->val), r_default,
							shape, last_level, level + 1);
					}
				
					l_node = l_node->next;
					r_node = r_node->next;
			
				} else if (l_node->key < r_node->key) {
					// Advance the left node knowing that the default value is OK.
			
					l_node = l_node->next;
					 
				} else /* if (l_node->key > r_node->key) */ {
					// Advance the right node knowing that the default value is OK.
			
					r_node = r_node->next;
				}
				
			} else {
				/*
				 * Our index needs to catch up but the default value is OK.  This
				 * conditional is here only for documentation and should be optimized
				 * out.
				 */
			}
		}
	}
}

