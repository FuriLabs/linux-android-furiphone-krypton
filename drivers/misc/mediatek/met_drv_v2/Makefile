MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
MTK_FAMILY_PLATFORM := $(MTK_PLATFORM)
ifeq ($(CONFIG_MACH_MT6833),y)
    MTK_PLATFORM := mt6833
endif
ifeq ($(CONFIG_MACH_MT6893),y)
    MTK_PLATFORM := mt6893
endif
ifeq ($(CONFIG_MACH_MT6877),y)
    MTK_PLATFORM := mt6877
endif
ifeq ($(CONFIG_MACH_MT6781),y)
    MTK_PLATFORM := mt6781
endif

MET_ROOT_DIR := $(srctree)/drivers/misc/mediatek/met_drv_v2
MET_COMMON_DIR := $(wildcard $(MET_ROOT_DIR)/common)
MET_BUILD_DEFAULT := n

ifneq ($(MTK_PLATFORM),)
    MET_PLF_DIR := $(wildcard $(MET_ROOT_DIR)/$(MTK_PLATFORM))
else
    MET_PLF_DIR :=
endif

ifeq ($(CONFIG_MODULES),y)

ifeq ($(CONFIG_FTRACE),y)
    ifeq ($(CONFIG_TRACING),y)
        FTRACE_READY := y
    endif
endif

ifeq ($(CONFIG_MTK_MET_PLF),m)
    MET_BUILD_KO := y
endif

ifneq ($(MET_PLF_DIR),)
    ifeq ($(FTRACE_READY),y)
        ifeq ($(MET_BUILD_KO),y)
            include $(MET_COMMON_DIR)/Kbuild
        else
            MET_BUILD_DEFAULT = y
        endif
    else
        MET_BUILD_DEFAULT = y
    endif
else
    MET_BUILD_DEFAULT = y
endif
else #CONFIG_MODULES = n
    MET_BUILD_DEFAULT := y
endif

ifeq ($(MET_BUILD_DEFAULT),y)
    MET_DEF_DIR := $(MET_ROOT_DIR)/default
    include $(MET_DEF_DIR)/Kbuild
endif
