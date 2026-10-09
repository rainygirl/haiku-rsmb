/* R SMB: Kerberos login to a Mac's Local KDC (LKDC) without GSSAPI.
 * Same entry points as libsmb2's krb5-wrapper, used through SMB2_SEC_KRB5.
 * LGPL 2.1, like the rest of libsmb2.
 */
#ifndef _RSMB_KRB5_H_
#define _RSMB_KRB5_H_

struct smb2_context;
struct private_auth_data;

void krb5_free_auth_data(struct private_auth_data *auth);
unsigned char *krb5_get_output_token_buffer(struct private_auth_data *auth);
int krb5_get_output_token_length(struct private_auth_data *auth);
struct private_auth_data *krb5_negotiate_reply(struct smb2_context *smb2, const char *server,
                                               const char *domain, const char *user_name,
                                               const char *password);
int krb5_session_get_session_key(struct smb2_context *smb2, struct private_auth_data *auth);
int krb5_session_request(struct smb2_context *smb2, struct private_auth_data *auth,
                         unsigned char *buf, int len);

#endif
