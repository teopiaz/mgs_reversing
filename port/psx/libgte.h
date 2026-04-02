#ifndef __PSX_LIBGTE_H__
#define __PSX_LIBGTE_H__

#include <sys/types.h>

/*---------------------------------------------------------------------------*/
/* Types                                                                     */
/*---------------------------------------------------------------------------*/

typedef struct {
    short vx, vy, vz;
    short pad;
} SVECTOR;

typedef struct {
    int vx, vy, vz;
    int pad;
} VECTOR;

typedef struct {
    short m[3][3];
    short pad;  /* explicit padding for 32-byte total */
    int   t[3];
} MATRIX;

typedef struct {
    u_char r, g, b, cd;
} CVECTOR;

typedef struct {
    short vx, vy;
} DVECTOR;

typedef struct {
    short x, y, w, h;
} RECT;

/*---------------------------------------------------------------------------*/
/* Constants                                                                 */
/*---------------------------------------------------------------------------*/

#define ONE 4096

/*---------------------------------------------------------------------------*/
/* Macros                                                                    */
/*---------------------------------------------------------------------------*/

#define setRECT(r, _x, _y, _w, _h) \
    (r)->x = (_x), (r)->y = (_y), (r)->w = (_w), (r)->h = (_h)

#define copyVector(v0, v1) \
    (v0)->vx = (v1)->vx, (v0)->vy = (v1)->vy, (v0)->vz = (v1)->vz

/*---------------------------------------------------------------------------*/
/* GTE initialization                                                        */
/*---------------------------------------------------------------------------*/

void InitGeom(void);
void SetGeomOffset(int ofx, int ofy);
void SetGeomScreen(int h);

/*---------------------------------------------------------------------------*/
/* Transform operations                                                      */
/*---------------------------------------------------------------------------*/

long RotTransPers(SVECTOR *v0, long *sxy, long *p, long *flag);
long RotTransPers3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
                   long *sxy0, long *sxy1, long *sxy2,
                   long *p, long *flag);
void RotTrans(SVECTOR *v0, VECTOR *v1, long *flag);
void LocalLight(SVECTOR *v0, VECTOR *v1);
void LightColor(VECTOR *v0, VECTOR *v1);

/*---------------------------------------------------------------------------*/
/* Normal / Color operations                                                 */
/*---------------------------------------------------------------------------*/

void NormalColor(SVECTOR *v0, CVECTOR *v1);
void NormalColor3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
                  CVECTOR *c0, CVECTOR *c1, CVECTOR *c2);
void NormalColorDpq(SVECTOR *v0, CVECTOR *v1, long p, CVECTOR *v2);
void NormalColorDpq3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
                     CVECTOR *v3, long p,
                     CVECTOR *v4, CVECTOR *v5, CVECTOR *v6);
void NormalColorCol(SVECTOR *v0, CVECTOR *v1, CVECTOR *v2);
void NormalColorCol3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
                     CVECTOR *v3, CVECTOR *v4, CVECTOR *v5, CVECTOR *v6);

/*---------------------------------------------------------------------------*/
/* Matrix operations                                                         */
/*---------------------------------------------------------------------------*/

void SetRotMatrix(MATRIX *m);
void SetTransMatrix(MATRIX *m);
void SetColorMatrix(MATRIX *m);
void SetLightMatrix(MATRIX *m);
void SetBackColor(long rbk, long gbk, long bbk);

MATRIX *MulMatrix0(MATRIX *m0, MATRIX *m1, MATRIX *m2);
MATRIX *MulMatrix(MATRIX *m0, MATRIX *m1);
MATRIX *MulMatrix2(MATRIX *m0, MATRIX *m1);
void CompMatrix(MATRIX *m0, MATRIX *m1, MATRIX *m2);
void CompMatrixLV(MATRIX *m0, MATRIX *m1, MATRIX *m2);

void TransMatrix(MATRIX *m, VECTOR *v);
void ScaleMatrix(MATRIX *m, VECTOR *v);
void ScaleMatrixL(MATRIX *m, VECTOR *v);

MATRIX *RotMatrix(SVECTOR *r, MATRIX *m);
MATRIX *RotMatrixX(long r, MATRIX *m);
MATRIX *RotMatrixY(long r, MATRIX *m);
MATRIX *RotMatrixZ(long r, MATRIX *m);
MATRIX *RotMatrixYXZ(SVECTOR *r, MATRIX *m);
MATRIX *RotMatrixZYX_gte(SVECTOR *r, MATRIX *m);
MATRIX *RotMatrixYXZ_gte(SVECTOR *r, MATRIX *m);

void ReadRotMatrix(MATRIX *m);

/*---------------------------------------------------------------------------*/
/* Apply operations                                                          */
/*---------------------------------------------------------------------------*/

void ApplyMatrix(MATRIX *m, SVECTOR *v0, VECTOR *v1);
void ApplyMatrixLV(MATRIX *m, VECTOR *v0, VECTOR *v1);
void ApplyMatrixSV(MATRIX *m, SVECTOR *v0, SVECTOR *v1);
void ApplyRotMatrix(SVECTOR *v0, VECTOR *v1);
void ApplyRotMatrixLV(VECTOR *v0, VECTOR *v1);

/*---------------------------------------------------------------------------*/
/* Vector operations                                                         */
/*---------------------------------------------------------------------------*/

void OuterProduct0(SVECTOR *v0, SVECTOR *v1, VECTOR *v2);
void OuterProduct12(VECTOR *v0, VECTOR *v1, VECTOR *v2);
long VectorNormal(VECTOR *v0, VECTOR *v1);
long VectorNormalS(VECTOR *v0, SVECTOR *v1);
long VectorNormalSS(SVECTOR *v0, SVECTOR *v1);
long Square0(SVECTOR *v0, VECTOR *v1);

/*---------------------------------------------------------------------------*/
/* Math operations                                                           */
/*---------------------------------------------------------------------------*/

int  rcos(int a);
int  rsin(int a);
/* Note: ccos/csin conflict with C99 complex.h builtins.
   The PSX SDK versions are just aliases for rcos/rsin. */
#define ccos rcos
#define csin rsin
long ratan2(long y, long x);
long SquareRoot0(long a);
long SquareRoot12(long a);

/*---------------------------------------------------------------------------*/
/* Utility                                                                   */
/*---------------------------------------------------------------------------*/

void PushMatrix(void);
void PopMatrix(void);
MATRIX *MulRotMatrix(MATRIX *m);
void SetMulMatrix(MATRIX *m);

#endif /* __PSX_LIBGTE_H__ */
