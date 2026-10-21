#ifndef Z7_LANGUAGE_PROFILE_H
#define Z7_LANGUAGE_PROFILE_H
#ifdef Z7_SOURCE_EN
#define CLOUD_INPUT_LANGUAGE "en"
#else
#define CLOUD_INPUT_LANGUAGE "zh"
#endif
#if defined(Z7_TRANSLATE_EN) || defined(Z7_TRANSLATE_ZH)
#define Z7_BILINGUAL
#endif
#if defined(Z7_BILINGUAL) && defined(Z7_ARM_TLS)
#define Z7_LANGUAGE_SWITCH
#endif
#if defined(Z7_SOURCE_EN) || defined(Z7_TRANSLATE_EN)
#define Z7_UI_ENGLISH
#endif
#endif
