//*****************************************************************************
//
// Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com/ 
// 
// 
//  Redistribution and use in source and binary forms, with or without 
//  modification, are permitted provided that the following conditions 
//  are met:
//
//    Redistributions of source code must retain the above copyright 
//    notice, this list of conditions and the following disclaimer.
//
//    Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the 
//    documentation and/or other materials provided with the   
//    distribution.
//
//    Neither the name of Texas Instruments Incorporated nor the names of
//    its contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
//  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
//  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
//  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
//  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
//  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
//  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
//  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
//  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
//  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
//  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//*****************************************************************************

#include <stdbool.h>
#include <stdint.h>
#include "rom.h"
#include "rom_map.h"
#include "hw_timer32.h"
#include "hw_ints.h"
#include "hw_types.h"
#include "timer.h"

/* To be removed */
/*
#define TIMER_TIMA_TIMEOUT 1
#define TIMER_TIMA_MATCH   2
#define TIMER_CFG_A_ONE_SHOT 1
#define TIMER_CFG_PERIODIC 1
#define TIMER_CFG_PERIODIC_UP 2
#define TIMER_A              7
*/
/* End remove */

static sys_irq_enbl enbl_irqc;
static sys_irq_dsbl dsbl_irqc;
static u32 sys_flags = 0;

struct hw_timer32 {
        
        u32                    base_addr;
        u32                    freq_hz;
        
        u32                    irq_mask;
        u32                    n_rollovers;

        cc_cb_fn               timeout_cb;
        cc_hndl                cb_param;

        enum cc_hw_timer_mode  op_mode;
        
        struct hw_timer32      *next;
};

static inline void hwt32_set_op_mode(struct hw_timer32     *hwt,
                                     enum cc_hw_timer_mode mode)
{
        hwt->op_mode = mode;
}

static inline bool hwt32_is_running(struct hw_timer32 *hwt)
{
        return HW_TIMER_NOT_USED == hwt->op_mode? false : true;
}

bool cc_hwt32_is_running(cc_hndl hndl)
{
        struct hw_timer32 *hwt = (struct hw_timer32*) hndl;
        bool rv = false;
        
        if(NULL == hwt)
                goto cc_hwt32_is_running_exit;
        
        dsbl_irqc(&sys_flags);
        rv = hwt32_is_running(hwt);
        enbl_irqc(sys_flags);

 cc_hwt32_is_running_exit:
        return rv;
}

static i32 hwt32_config_one_shot(struct hw_timer32 *hwt, u32 expires)
{
        /* Count downwards from value of 'expires' to zero */
        MAP_TimerConfigure(hwt->base_addr, TIMER_CFG_A_ONE_SHOT);
        MAP_TimerLoadSet(hwt->base_addr, TIMER_A, expires); /* Countdown value */
        
        hwt->irq_mask = TIMER_TIMA_TIMEOUT;  /* IRQ for counter reaching 0 */

        return 0;
}

static i32 hwt32_config_periodic(struct hw_timer32 *hwt, u32 expires)
{        
        /* Count downwards from value of 'expires' to zero */
        MAP_TimerConfigure(hwt->base_addr, TIMER_CFG_PERIODIC);
        MAP_TimerLoadSet(hwt->base_addr, TIMER_A, expires);

        hwt->irq_mask = TIMER_TIMA_TIMEOUT; /* IRQ for counter reaching 0 */
        
        return 0;
}

static i32 hwt32_config_monotone(struct hw_timer32 *hwt, u32 expires)
{        
        /* Count upwards until counter value matches 'expires' */
        MAP_TimerConfigure(hwt->base_addr, TIMER_CFG_PERIODIC_UP);
        MAP_TimerLoadSet(hwt->base_addr, TIMER_A, 0xFFFFFFFF); /* Counter Rollover */
        MAP_TimerMatchSet(hwt->base_addr, TIMER_A, expires);   /* User match value */

        /* IRQ(s) for rollovers and couner matching 'expires' */
        hwt->irq_mask = TIMER_TIMA_TIMEOUT | TIMER_TIMA_MATCH;
        
        return 0;
}

static i32 hwt32_configure(struct hw_timer32 *hwt, u32 expires,
                           enum cc_hw_timer_mode mode)
{
        i32 rv = 0;

        switch(mode) {

        case HW_TIMER_ONE_SHOT:
                rv = hwt32_config_one_shot(hwt, expires);
                break;

        case HW_TIMER_PERIODIC:
                rv = hwt32_config_periodic(hwt, expires);
                break;

        case HW_TIMER_MONOTONE:
                rv = hwt32_config_monotone(hwt, expires);
                break;

        default:
                rv = -1;
                break;
        }

        return rv;
}

static i32 hwt32_start(struct hw_timer32 *hwt, u32 expires, 
                       enum cc_hw_timer_mode mode)
{
        if(hwt32_configure(hwt, expires, mode))
                return -1;

        hwt32_set_op_mode(hwt, mode);
        
        MAP_TimerIntEnable(hwt->base_addr, hwt->irq_mask);
        MAP_TimerEnable(hwt->base_addr, TIMER_A);

        return 0;
}

i32 cc_hwt32_start(cc_hndl hndl, u32 expires, enum cc_hw_timer_mode mode)
{
        struct hw_timer32 *hwt = (struct hw_timer32*) hndl;
        i32 rv = -1;
        
        dsbl_irqc(&sys_flags);

        if(hwt && (false == hwt32_is_running(hwt))) {
                rv = hwt32_start(hwt, expires, mode);
        }

        enbl_irqc(sys_flags);

        return rv;
}

static i32 hwt32_update(struct hw_timer32 *hwt, u32 expires)
{
        i32 rv = 0;

        switch(hwt->op_mode) {

        case HW_TIMER_ONE_SHOT:
        case HW_TIMER_PERIODIC:
                MAP_TimerLoadSet(hwt->base_addr, TIMER_A, expires);
                break;

        case HW_TIMER_MONOTONE:
                MAP_TimerMatchSet(hwt->base_addr, TIMER_A, expires);
                break;

        default:
                rv = -1;
                break;
        }
        
        return rv;
}

i32 cc_hwt32_update(cc_hndl hndl, u32 expires)
{
        struct hw_timer32 *hwt = (struct hw_timer32*) hndl;
        i32 rv = -1;

        dsbl_irqc(&sys_flags);

        if(hwt && (true == hwt32_is_running(hwt))) {
                rv = hwt32_update(hwt, expires);
        }

        enbl_irqc(sys_flags);

        return rv;
}

static i32 hwt32_stop(struct hw_timer32* hwt)
{
        MAP_TimerDisable(hwt->base_addr, TIMER_A);
        MAP_TimerIntDisable(hwt->base_addr, hwt->irq_mask);
        
        hwt32_set_op_mode(hwt, HW_TIMER_NOT_USED);
        hwt->n_rollovers = 0;

        return 0;
}

i32 cc_hwt32_stop(cc_hndl hndl)
{
        struct hw_timer32 *hwt = (struct hw_timer32*) hndl;
        i32 rv = -1;

        dsbl_irqc(&sys_flags);

        if(hwt && (true == hwt32_is_running(hwt))) {
                rv = hwt32_stop(hwt);
        }

        enbl_irqc(sys_flags);

        return rv;
}

/* utility function: move to utils */
static u32 u32_wrap_offset_v1_to_v2(u32 v1, u32 v2)
{
        return (v2 > v1)? v2 - v1 : v1 + ~v2 + 1;
}

static i32 hwt32_get_remaining(struct hw_timer32 *hwt, u32 *remaining)
{
        i32 rv = 0;
        
        switch(hwt->op_mode) {
                
        case HW_TIMER_ONE_SHOT:
        case HW_TIMER_PERIODIC:
                *remaining = MAP_TimerValueGet(hwt->base_addr, TIMER_A);
                break;

        case HW_TIMER_MONOTONE:
                *remaining = u32_wrap_offset_v1_to_v2(
                                        MAP_TimerMatchGet(hwt->base_addr,
                                        				  TIMER_A),
                                        MAP_TimerValueGet(hwt->base_addr,
                                        				  TIMER_A));
                break;

        default:
                rv = -1;
                break;
        }        

        return rv;
}

i32 cc_hwt32_get_remaining(cc_hndl hndl, u32 *remaining)
{
        i32 rv = 0;

        struct hw_timer32 *hwt = (struct hw_timer32*) hndl;

        dsbl_irqc(&sys_flags);
        
        if(hwt && (true == hwt32_is_running(hwt))) {
                rv = hwt32_get_remaining(hwt, remaining);
        }

        enbl_irqc(sys_flags);

        return rv;
}

i32 hwt32_get_current(struct hw_timer32 *hwt, u32 *current)
{
        i32 rv = 0;

        switch(hwt->op_mode) {

        case HW_TIMER_ONE_SHOT:
        case HW_TIMER_PERIODIC:
                *current = MAP_TimerLoadGet(hwt->base_addr, TIMER_A) -
                		   MAP_TimerValueGet(hwt->base_addr, TIMER_A);
                break;

        case HW_TIMER_MONOTONE:
                *current = MAP_TimerValueGet(hwt->base_addr, TIMER_A);
                break;

        default:
                rv = -1;
                break;
        }

        return rv;
}

i32 cc_hwt32_get_current(cc_hndl hndl, u32 *current)
{
        struct hw_timer32 *hwt = (struct hw_timer32*) hndl;
        i32 rv = -1;

        dsbl_irqc(&sys_flags);

        if(hwt && (true == hwt32_is_running(hwt))) {
                *current = MAP_TimerValueGet(hwt->base_addr, TIMER_A);
                rv = hwt32_get_current(hwt, current);
        }

        enbl_irqc(sys_flags);

        return rv;
}

u32 cc_hwt32_get_rollovers(cc_hndl hndl)
{
        struct hw_timer32 *hwt = (struct hw_timer32*) hndl;
        u32 rv = 0;

        dsbl_irqc(&sys_flags);

        if(hwt && (true == hwt32_is_running(hwt)))
                rv = hwt->n_rollovers;

        enbl_irqc(sys_flags);
        
        return rv;
}

u32 cc_hwt32_get_frequency(cc_hndl hndl)
{
        return hndl? ((struct hw_timer32*) hndl)->freq_hz : 0;
}

i32 cc_hwt32_register_cb(cc_hndl hndl, cc_cb_fn user_cb, cc_hndl cb_param)
{
        struct hw_timer32 *hwt = (struct hw_timer32*) hndl;

        if((NULL == hwt) || (NULL != hwt->timeout_cb))
                return -1;  /* Callback was aleady configured */

        hwt->timeout_cb = user_cb;
        hwt->cb_param   = cb_param;

        return 0;
}

static struct hw_timer32 timers32[MAX_HW_TIMER32];
static struct hw_timer32 *hwt_list = NULL;
static bool   init_flag = false;

static void hwt32_setup(void)
{
        i32 i = 0;
        for(i = 0; i < MAX_HW_TIMER32; i++) {
                struct hw_timer32 *hwt = timers32 + i;
                hwt->next = hwt_list;
                hwt_list  = hwt;
        }

        return;
}

DEF_HWT32_OPS(cc_hwt32_start,         cc_hwt32_update,        cc_hwt32_stop, 
              cc_hwt32_is_running,    cc_hwt32_get_remaining, 
              cc_hwt32_get_current,   cc_hwt32_get_rollovers, 
              cc_hwt32_get_frequency, cc_hwt32_register_cb)

cc_hndl cc_hwt32_init(struct hw_timer_cfg *cfg)
{
        struct hw_timer32 *hwt = NULL;

        if(false == init_flag) {
                memset(timers32, 0, sizeof(timers32));
                hwt32_setup();
                init_flag = true;
        }

        hwt = hwt_list;
        if((!hwt) || (!cfg) || (!cfg->user_tfw && !cfg->cb.timeout_cb))
                return NULL;

        if(cfg->user_tfw && 
           cfg->cb.tfw_register_hwt_ops(cfg->source, (cc_hndl)hwt, &hwt_ops)) {
                return NULL;
        } else {
                hwt->timeout_cb = cfg->cb.timeout_cb;
                hwt->cb_param   = cfg->cb_param;
        }
        
        hwt_list  = hwt->next;
        hwt->next = NULL;

        hwt->base_addr  = cfg->base_addr;
        hwt->freq_hz    = cfg->freq_hz;

        enbl_irqc       = cfg->enbl_irqc;
        dsbl_irqc       = cfg->dsbl_irqc;

        hwt32_set_op_mode(hwt, HW_TIMER_NOT_USED);

        return (cc_hndl) hwt;
}

/* Called in interrupt context */
void hwt32_handle_timeout(struct hw_timer32 *hwt)
{
        switch(hwt->op_mode) {

        case HW_TIMER_ONE_SHOT:
                hwt->timeout_cb(hwt->cb_param);
                hwt32_stop(hwt);
                break;

        case HW_TIMER_PERIODIC:
        case HW_TIMER_MONOTONE:
                hwt->timeout_cb(hwt->cb_param);
                break;

        default:
                break;
        }
        
        return;
}

void cc_hwt32_isr(cc_hndl hndl)
{
        u32 status;

        struct hw_timer32 *hwt = (struct hw_timer32*) hndl;

        if(NULL == hwt)
                return;

        status = MAP_TimerIntStatus(hwt->base_addr, true);

        if((false == hwt32_is_running(hwt)) ||
           (status & ~hwt->irq_mask))  
                goto hwt32_isr_exit;
        
        if((HW_TIMER_MONOTONE == hwt->op_mode) &&
           (status & TIMER_TIMA_TIMEOUT))
                hwt->n_rollovers++;
                
        hwt32_handle_timeout(hwt);
        
 hwt32_isr_exit:
        MAP_TimerIntClear(hwt->base_addr, status);
        
        return;
}
