#ifndef VECTOR_H
#define VECTOR_H

struct vector
{
	double x;
	double y;
	double z;
};

typedef struct vector Matrix[3];
typedef struct vector Vector;

void mult_matrix (struct vector *first, struct vector *second);
void mult_vector (struct vector *vec, struct vector *mat);
double vector_dot_product (struct vector *first, struct vector *second);
struct vector unit_vector (struct vector *vec);
void set_init_matrix (struct vector *mat);
void tidy_matrix (struct vector *mat);

#endif

