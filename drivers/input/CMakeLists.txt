# Copyright (c) 2026 ErgoDox Wireless Project
# SPDX-License-Identifier: MIT

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_SYNAPTICS_TM_P3125 input_synaptics_tm_p3125.c)
