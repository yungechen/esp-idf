#ifndef __OSAL_H__
#define __OSAL_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CHK_BIT
#define CHK_BIT(msk,bit) (((msk) & (bit)) != 0)
#endif

#ifndef CLR_BIT
#define CLR_BIT(msk,bit) ((msk) &= ~(bit))
#endif

#ifndef SET_BIT
#define SET_BIT(msk,bit) ((msk) |= (bit))
#endif

#define MSG_BIT(msg)              (1u << (msg))
void osal_init(void);

#ifdef __cplusplus
}
#endif
#endif