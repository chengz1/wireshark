/* packet-wlancertextn.c
 * Routines for Wireless Certificate Extension (RFC3770)
 *  Ronnie Sahlberg 2005
 *
 * $Id: packet-wlancertextn-template.c 12434 2004-10-29 12:11:42Z sahlberg $
 *
 * Ethereal - Network traffic analyzer
 * By Gerald Combs <gerald@ethereal.com>
 * Copyright 1998 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib.h>
#include <epan/packet.h>
#include <epan/conversation.h>

#include <stdio.h>
#include <string.h>

#include "packet-ber.h"
#include "packet-wlancertextn.h"
#include "packet-x509af.h"
#include "packet-x509ce.h"
#include "packet-x509sat.h"

#define PNAME  "Wlan Certificate Extension"
#define PSNAME "WLANCERTEXTN"
#define PFNAME "wlancertextn"

/* Initialize the protocol and registered fields */
int proto_wlancertextn = -1;
#include "packet-wlancertextn-hf.c"

/* Initialize the subtree pointers */
#include "packet-wlancertextn-ett.c"

#include "packet-wlancertextn-fn.c"


/*--- proto_register_wlancertextn ----------------------------------------------*/
void proto_register_wlancertextn(void) {

  /* List of fields */
  static hf_register_info hf[] = {
#include "packet-wlancertextn-hfarr.c"
  };

  /* List of subtrees */
  static gint *ett[] = {
#include "packet-wlancertextn-ettarr.c"
  };

  /* Register protocol */
  proto_wlancertextn = proto_register_protocol(PNAME, PSNAME, PFNAME);

  /* Register fields and subtrees */
  proto_register_field_array(proto_wlancertextn, hf, array_length(hf));
  proto_register_subtree_array(ett, array_length(ett));

}


/*--- proto_reg_handoff_wlancertextn -------------------------------------------*/
void proto_reg_handoff_wlancertextn(void) {
#include "packet-wlancertextn-dis-tab.c"
  register_ber_oid_name("1.3.6.1.5.5.7.3.13","id-kp-eapOverPPP");
  register_ber_oid_name("1.3.6.1.5.5.7.3.14","id-kp-eapOverLAN");
}

