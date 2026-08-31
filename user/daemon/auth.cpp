#include "auth.h"

static int conv_func(int num_msg, const pam_message **msg, pam_response **resp,
                     void *appdata_ptr) {
    const char *pw = (const char *)appdata_ptr;
    // allocate for the response
    *resp = (pam_response *)calloc((size_t)num_msg, sizeof(pam_response));
    if (!*resp)
        return PAM_BUF_ERR;

    for (int i = 0; i < num_msg; i++) {
        // check if PAM ask for password
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF) {
            // copy over password
            (*resp)[i].resp = strdup(pw ? pw : "");
            if (!(*resp)[i].resp) {
                return PAM_BUF_ERR;
            }
        }
        //ignore other requests
    }
    // we done with the response
    return PAM_SUCCESS;
}

bool authenticate(const char *user, const char *password) {
    pam_handle_t *pamh = nullptr;
    pam_conv conv = {conv_func, (void *)password};

    int ret = pam_start("login", user, &conv, &pamh);
    if (ret != PAM_SUCCESS)
        return false;

    ret = pam_authenticate(pamh, 0);
    if (ret == PAM_SUCCESS)
        ret = pam_acct_mgmt(pamh, 0);

    pam_end(pamh, ret);
    return ret == PAM_SUCCESS;
}
