#include <stdio.h>
#include "common.h"
#include "game/game.h"
#include "menu/menuman.h"
#include "mts/mts.h"
#include "mts/taskid.h"

#ifdef __mips
#define OFFSET_TO_PTR2(ptr, offset) ((int)(offset) = (int)(ptr) + (int)(offset))
#else
#define OFFSET_TO_PTR2(ptr, offset) (*(intptr_t *)&(offset) = (intptr_t)(ptr) + (intptr_t)(offset))
#endif

extern menu_0x14 stru_800BDA48[2];

int sub_80046C90(menu_chara_struct_sub *pSub, int idx, face_full_anim *pFullAnim, int pFrameNum)
{
    face_full_anim_frame *fullAnimFrame;
    short                 field_10;
    unsigned int          tmp;

    while (1)
    {
        if (pFrameNum >= pFullAnim->field_0_frame_count)
        {
            return 0;
        }

        fullAnimFrame = &pFullAnim->field_4_frames[pFrameNum];
        if (fullAnimFrame->field_0_palette == NULL && fullAnimFrame->field_4_image == NULL)
        {
            tmp = fullAnimFrame->field_8;
            switch (tmp)
            {
            case 0:
                GM_SeSet2(0, 0x3f, fullAnimFrame->field_10);
                pFrameNum++;
                break;

            case 1:
                if (pSub->field_4C_mouthAnimFrame == 0)
                {
                    pSub->field_4C_mouthAnimFrame = fullAnimFrame->field_10;
                }

                if (pSub->field_4C_mouthAnimFrame < 255)
                {
                    if (pSub->field_4C_mouthAnimFrame == 1)
                    {
                        return 0;
                    }
                    pSub->field_4C_mouthAnimFrame--;
                }
                pFrameNum = 0;
                break;

            default:
                pFrameNum++;
                break;
            }
        }
        else
        {
            if (fullAnimFrame->field_10 > 0)
            {
                sub_80046BD8(idx);
            }
            menu_radio_load_palette_80046B74(fullAnimFrame->field_0_palette, idx);
            LoadFaceAnimImage_80046B10(fullAnimFrame->field_4_image, idx);

            pSub->field_E_eyesAnimFrame = fullAnimFrame->field_8;

            tmp = pFrameNum;
            field_10 = fullAnimFrame->field_10;

            pSub->field_8_animFrameNum = tmp;
            pSub->field_A = field_10;
            pSub->field_C = field_10;
            return 1;
        }
    }
}

void menu_radio_draw_face_helper_helper_80046DF4(int idx, menu_chara_struct *pChara, int chara, int code, int a5)
{
    int                    i;
    face_header           *faceIter;
    face_simple_anim      *simpleAnim;
    face_full_anim        *fullAnim;
    menu_chara_struct_sub *pSub;

    pSub = &pChara->field_3C[idx];

    if (code == 0)
    {
        pSub->field_0_animState = 3;
        pSub->field_14_face_anim.raw_ptr = NULL;
        pSub->field_2_chara = chara;
    }
    else
    {
        faceIter = pChara->field_34_faces;

        for (i = pChara->field_30_face_count; i > 0; faceIter++, i--)
        {
            if (faceIter->field_2_code == code)
            {
                pSub->field_2_chara = chara;

                switch (faceIter->field_0_anim_type)
                {
                case FACE_ANIM_SIMPLE:
                    simpleAnim = faceIter->field_8_anim_data.simple_anim;
                    if (a5 >= 0)
                    {
                        sub_80046BD8(idx);
                    }

                    menu_radio_load_palette_80046B74(simpleAnim->field_0_palette, idx);
                    LoadFaceAnimImage_80046B10(simpleAnim->field_4_face, idx);
                    pSub->field_0_animState = 1;
                    pSub->field_A = a5;
                    pSub->field_C = a5;
                    pSub->field_14_face_anim.simple_anim = simpleAnim;
                    pSub->field_4C_mouthAnimFrame = 0;
                    pSub->field_E_eyesAnimFrame = 0;
                    return;

                case FACE_ANIM_FULL:
                    fullAnim = faceIter->field_8_anim_data.full_anim;
                    pSub->field_8_animFrameNum = 0;
                    pSub->field_4C_mouthAnimFrame = 0;
                    sub_80046C90(pSub, idx, fullAnim, pSub->field_8_animFrameNum);
                    pSub->field_0_animState = 2;
                    pSub->field_14_face_anim.full_anim = fullAnim;
                    return;

                default:
                    return;
                }
            }
        }

        printf("NO_FACE_DATA\n");
    }
}

#ifdef PORT_BUILD
/* Port: FACE.DAT uses PSX 32-bit struct layout. Parse with PSX field sizes
   and convert offsets to native pointers.
   PSX face_header = 12 bytes: {u16 type, u16 code, i32 field4, u32 anim_off}
   PSX face_simple_anim = 32 bytes: 8 x u32 offsets
   PSX face_full_anim_frame = 12 bytes: {u32 palette_off, u32 image_off, i16, i16} */
#include <string.h>
#define PSX_FACE_HEADER_SIZE 12
#define PSX_SIMPLE_ANIM_PTRS 8
#define PSX_FULL_FRAME_SIZE  12

static uint32_t read_u32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t read_u16(const void *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static int32_t  read_i32(const void *p) { int32_t v;  memcpy(&v, p, 4); return v; }

void menu_radio_codec_task_proc_helper_80046F3C(menu_chara_struct *pStru, faces_group *pFacesGroup)
{
    unsigned char *raw = (unsigned char *)pFacesGroup;
    int face_count = read_i32(raw);
    unsigned char *faces_raw = raw + 4;

    /* We need to store native face_header array. Allocate over the raw data
       since we won't need the PSX layout after conversion.
       But the raw data also contains the animation data that pointers reference,
       so we can't overwrite it. Instead, build native face_header array in-place
       at the start (expanding from 12 to 16 bytes each), working backwards. */

    /* Actually, we can't expand in-place safely. Instead, just store the raw
       pointer and use PSX-stride iteration everywhere. The simplest fix is
       to make face_header and related structs use 32-bit fields for the port. */

    /* Simpler approach: just iterate using PSX stride and convert offsets to
       native pointers directly in the raw data. The code that uses face_header
       later accesses field_0_anim_type (at +0), field_2_code (at +2), field_4
       (at +4), and field_8_anim_data (at +8). On PSX this is a 4-byte offset.
       We can store a truncated pointer index there.
       But the cleanest approach: build a native face_header array. */

    int i, j;
    static face_header native_faces[64]; /* max faces per codec call */
    /* Port: sanity-clamp face_count. A non-Integral disc image can produce
       garbage here because the game's radio table addresses RADIO.DAT sectors
       beyond the vanilla file's physical size; the parser would then walk
       arbitrary bytes and eventually fault. Fail soft instead. */
    if (face_count < 0 || face_count > 64) {
        face_count = (face_count < 0) ? 0 : 64;
    }

    pStru->field_30_face_count = face_count;
    pStru->field_34_faces = native_faces;

    for (i = 0; i < face_count; i++)
    {
        unsigned char *fh = faces_raw + i * PSX_FACE_HEADER_SIZE;
        native_faces[i].field_0_anim_type = read_u16(fh + 0);
        native_faces[i].field_2_code      = read_u16(fh + 2);
        native_faces[i].field_4           = read_i32(fh + 4);

        /* field_8 is a relative offset from faces_raw to the anim data.
           Port: clamp offsets clearly outside a reasonable pFacesGroup so
           a garbage RADIO.DAT read doesn't produce wild pointers. The real
           MGS FACE groups are at most a few hundred KB. */
        uint32_t anim_off = read_u32(fh + 8);
        if (anim_off > (4 * 1024 * 1024)) {
            native_faces[i].field_0_anim_type = 0;
            native_faces[i].field_8_anim_data.raw_ptr = NULL;
            continue;
        }
        unsigned char *anim_base = faces_raw + anim_off;
        native_faces[i].field_8_anim_data.raw_ptr = anim_base;

        switch (native_faces[i].field_0_anim_type)
        {
        case FACE_ANIM_SIMPLE:
        {
            /* simple_anim has 8 x u32 offsets from anim_base.
               Convert each non-zero offset to a real pointer.
               The native face_simple_anim has 8 native pointers. */
            static face_simple_anim native_simple[64];
            unsigned char *sa = anim_base;
            face_simple_anim *ns = &native_simple[i];
            unsigned char **dst = (unsigned char **)ns;
            for (j = 0; j < PSX_SIMPLE_ANIM_PTRS; j++)
            {
                uint32_t off = read_u32(sa + j * 4);
                dst[j] = off ? (anim_base + off) : NULL;
            }
            native_faces[i].field_8_anim_data.simple_anim = ns;
            break;
        }
        case FACE_ANIM_FULL:
        {
            /* full_anim starts with frame_count (i32), then frames.
               Each PSX frame = 12 bytes: {u32 pal_off, u32 img_off, i16, i16} */
            static face_full_anim native_full;
            static face_full_anim_frame native_frames[256];
            int fc = read_i32(anim_base);
            native_full.field_0_frame_count = fc;
            native_full.field_4_frames[0] = native_frames[0]; /* dummy for flex array */
            printf("frame num %d\n", fc);
            if (fc > 256) fc = 256;
            unsigned char *fr = anim_base + 4;
            for (j = 0; j < fc; j++)
            {
                unsigned char *fp = fr + j * PSX_FULL_FRAME_SIZE;
                uint32_t pal_off = read_u32(fp + 0);
                uint32_t img_off = read_u32(fp + 4);
                native_frames[j].field_0_palette = pal_off ? (unsigned char *)(anim_base + pal_off) : NULL;
                native_frames[j].field_4_image   = img_off ? (face_anim_image *)(anim_base + img_off) : NULL;
                memcpy(&native_frames[j].field_8, fp + 8, 2);
                memcpy(&native_frames[j].field_10, fp + 10, 2);
            }
            /* Point face_header to our native full_anim.
               Hack: store pointer to native_full, but its field_4_frames is flex array.
               Instead, just store a pointer to native_frames-4 so that
               ((face_full_anim*)ptr)->field_4_frames == native_frames. */
            native_faces[i].field_8_anim_data.full_anim = (face_full_anim *)(void *)((char *)native_frames - offsetof(face_full_anim, field_4_frames));
            native_faces[i].field_8_anim_data.full_anim->field_0_frame_count = fc;
            break;
        }
        }
    }
}
#else
void menu_radio_codec_task_proc_helper_80046F3C(menu_chara_struct *pStru, faces_group *pFacesGroup)
{
    int                   i, j;
    face_header          *faces;
    face_anim             anim;
    face_header          *facesIter;
    void                **simpleAnimIter;
    face_full_anim       *fullAnim;
    face_full_anim_frame *fullAnimIter;

    i = pStru->field_30_face_count = pFacesGroup->field_0_face_count;
    faces = pStru->field_34_faces = pFacesGroup->field_4_faces;
    facesIter = faces;

    for (; i > 0; facesIter++, i--)
    {
        anim.intptr = OFFSET_TO_PTR2(faces, facesIter->field_8_anim_data.raw_ptr);

        switch (facesIter->field_0_anim_type)
        {
        case FACE_ANIM_SIMPLE:
            simpleAnimIter = (void *) anim.simple_anim;
            for (j = 8; j > 0; j--, simpleAnimIter++)
            {
                if (*simpleAnimIter != NULL)
                {
                    OFFSET_TO_PTR2(facesIter->field_8_anim_data.raw_ptr, *simpleAnimIter);
                }
            }
            break;

        case FACE_ANIM_FULL:
            fullAnim = anim.full_anim;
            printf("frame num %d\n", fullAnim->field_0_frame_count);
            fullAnimIter = fullAnim->field_4_frames;
            for (j = 0; j < fullAnim->field_0_frame_count; fullAnimIter++, j++)
            {
                if (fullAnimIter->field_0_palette != NULL)
                {
                    OFFSET_TO_PTR2(facesIter->field_8_anim_data.raw_ptr, fullAnimIter->field_0_palette);
                }
                if (fullAnimIter->field_4_image != NULL)
                {
                    OFFSET_TO_PTR2(facesIter->field_8_anim_data.raw_ptr, fullAnimIter->field_4_image);
                }
            }
            break;
        }
    }
}
#endif

unsigned char *menu_gcl_read_word_80047098(int *pOut, unsigned char *pScript)
{
    *pOut = (pScript[1]) | (pScript[0] << 8);
    return pScript + sizeof(short);
}

//menu_set_struct
void menu_800470B4(int idx, menu_chara_struct *pStru, int chara, int code, int faceUnk, int taskWup)
{
    stru_800BDA48[idx].field_0_bUnknown = 1;
    stru_800BDA48[idx].field_10_pCharaStru = pStru;
    stru_800BDA48[idx].field_4_chara = chara;
    stru_800BDA48[idx].field_8_code = code;
    stru_800BDA48[idx].field_C = faceUnk;
    stru_800BDA48[idx].field_2_bTaskWup = taskWup;
}

void menu_radio_draw_face_helper_800470F4(int idx)
{
  if (stru_800BDA48[idx].field_0_bUnknown)
  {
    menu_radio_draw_face_helper_helper_80046DF4(idx, stru_800BDA48[idx].field_10_pCharaStru, stru_800BDA48[idx].field_4_chara, stru_800BDA48[idx].field_8_code, stru_800BDA48[idx].field_C);
    if (!stru_800BDA48[idx].field_8_code)
    {
      if (stru_800BDA48[idx].field_2_bTaskWup)
      {
        mts_wup_tsk(MTSID_CD_READ);
        stru_800BDA48[idx].field_2_bTaskWup = 0;
      }
    }
    stru_800BDA48[idx].field_0_bUnknown = 0;
  }
}