#include <linux/if_link.h>
#include "linux/xfrm.h"
#include "err.h"

#if defined(NETKEY_SUPPORT) && defined(USE_XFRM_INTERFACE)
/* how to check defined(XFRMA_IF_ID) && defined(IFLA_XFRM_LINK)? those are enums */

/* xfrmi interface format. start with ipsec0. IFNAMSIZ - 1 */
#define XFRMI_DEV_FORMAT "ipsec%" PRIu32
struct connection;
extern bool setup_xfrm_interface(struct connection *c);
extern bool ip_link_set_up(const char *if_name);
extern bool stale_xfrmi_interfaces(void);
extern err_t xfrm_iface_supported(void);
extern void free_xfrmi_ipsec0(void);
#else
# error this file should only be included when NETKEY_SUPPORT & USE_XFRM_INTERFACE are defined
#endif
