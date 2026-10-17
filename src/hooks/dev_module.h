/* The developer run module, rebuilt from the host. See dev_module.c. */

#ifndef DEFJAM_DEV_MODULE_H
#define DEFJAM_DEV_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

void dev_module_request(void);                  /* queue it at the next quiet manager tick */
int  dev_module_active(void);                   /* it is the current run module */
void dev_module_after_call(const char *text);   /* the front-end call hook: arms on RECOMP_DEV_MODULE's anchor */
void dev_module_manager_enter(void);            /* _enter of the manager's tick sub_00077040 */
void dev_module_trace(int slot);                /* _enter of our methods: 1 Load, 3 Update, 4 Render, 7 Unload */

#ifdef __cplusplus
}
#endif

#endif
