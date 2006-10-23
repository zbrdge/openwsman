/*******************************************************************************
 * Copyright (C) 2004-2006 Intel Corp. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  - Neither the name of Intel Corp. nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL Intel Corp. OR THE CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *******************************************************************************/

/**
 * @author Anas Nashif
 * @author Eugene Yarmosh
 * @author Vadim Revyakin
 */
#include "wsman_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include "u/libu.h"
#include "wsman-xml-api.h"
#include "wsman-errors.h"
#include "wsman-soap.h"
#include "wsman-xml.h"
#include "wsman-xml-serializer.h"

#include "wsman-client.h"
#include "wsman-client-options.h"
#include "wsman.h"
#include "wsman-client-transport.h"


int facility = LOG_DAEMON;



static void wsman_output(WsXmlDocH doc)
{
    FILE *f;
    const char *filename = wsman_options_get_output_file();
    if (filename) {
        f  = fopen(filename , "w+");
        if (f == NULL)
            error("Could not open file for writing");
        else
        ws_xml_dump_node_tree(f, ws_xml_get_doc_root(doc));
    } else {
        ws_xml_dump_node_tree(stdout, ws_xml_get_doc_root(doc));
    }
    return;
}


static void
debug_message_handler (const char *str, debug_level_e level, void  *user_data)
{
    if (level <= wsman_options_get_debug_level ()) 
    {
        struct tm *tm;
        time_t now;
        char timestr[128];

        time (&now);
        tm = localtime (&now);
        strftime (timestr, 128, "%b %e %T", tm);
        fprintf (stderr, "%s  %s\n", timestr, str);
    }

}

static void
initialize_logging (void)
{
    debug_add_handler (debug_message_handler, DEBUG_LEVEL_ALWAYS, NULL);
} 



int main(int argc, char** argv)
{
    int retVal = 0;
	int op;
    char *filename;
    dictionary       *ini = NULL;
	WsManClient *cl;
    WsXmlDocH doc;
    char *enumContext;
    WsXmlDocH rqstDoc;
    actionOptions options;    
    int optimize_max_elements = 0;	
    char *enumeration_mode, *binding_enumeration_mode, *resource_uri_with_selectors;
	char *resource_uri = NULL;
	
	initialize_action_options(&options);

    if (!wsman_parse_options(argc, argv))
        exit(EXIT_FAILURE);

    filename = (char *)wsman_options_get_config_file();
    if (filename)  {
        ini = iniparser_load(filename);
        printf("Using conf file: %s\n", filename);
        if (ini==NULL) {
            fprintf(stderr, "cannot parse file [%s]", filename);
            exit(EXIT_FAILURE);
        } else if (!wsman_read_client_config(ini)) {
            fprintf(stderr, "Configuration file not found\n");
            exit(EXIT_FAILURE);
        }
    }

    wsman_setup_transport_and_library_options();

    initialize_logging ();
    wsman_client_transport_init(NULL);
    WsContextH cntx = ws_create_runtime(NULL);
    
    debug( "Certificate: %s", wsman_options_get_cafile());

    if (wsman_options_get_cafile() != NULL)
	{
        cl = wsman_connect_with_ssl( cntx, wsman_options_get_server(),
                    wsman_options_get_server_port(),
                    wsman_options_get_path(),
                    "https",
                    wsman_options_get_username(),
                    wsman_options_get_password(),
                    wsman_options_get_cafile(),
                    NULL,
                    NULL);
    } else {
        cl = wsman_connect( cntx, wsman_options_get_server(),
                    wsman_options_get_server_port(),
                    wsman_options_get_path(),
                    "http",
                    wsman_options_get_username(),
                    wsman_options_get_password(),
                    NULL);
    }


    if (cl == NULL) {
        error("Null Client");
        exit(EXIT_FAILURE);
    }


    wsman_client_add_handler(wsman_client_handler, NULL);

	/*
	 * Setup Resource URI and Selectors
	 */
    resource_uri_with_selectors = wsman_options_get_resource_uri();
	wsman_set_options_from_uri(resource_uri_with_selectors, &options);
	wsman_remove_query_string(resource_uri_with_selectors, &resource_uri);

    op = wsman_options_get_action();

    if (wsman_options_get_dump_request()) {
        options.flags |= FLAG_DUMP_REQUEST;
    }
    if (wsman_options_get_max_envelope_size()) {
        options.max_envelope_size = wsman_options_get_max_envelope_size();
    }
    if (wsman_options_get_operation_timeout()) {
        options.timeout = wsman_options_get_operation_timeout();
    }
    if (wsman_options_get_fragment()) {
        options.fragment = wsman_options_get_fragment();
    }
    if (wsman_options_get_filter()) {
        options.filter = wsman_options_get_filter();
    }
    if (wsman_options_get_dialect()) {
        options.dialect = wsman_options_get_dialect();
    }
    options.cim_ns = wsman_options_get_cim_namespace();


    switch (op) 
    {
    case  ACTION_TEST:
        rqstDoc = ws_xml_read_file(ws_context_get_runtime(cntx),
                        wsman_options_get_test_file(), "UTF-8", 0 );
        doc = ws_send_get_response(cl, rqstDoc, 60000);
        if (doc) {
            wsman_output(doc);
			ws_xml_destroy_doc(doc);
        }
        break;
    case  ACTION_IDENTIFY:
        doc = cl->ft->identify(cl, options);
        if (doc) {
            wsman_output(doc);
			ws_xml_destroy_doc(doc);
        }
        break;
    case  ACTION_INVOKE:
        doc = cl->ft->invoke(cl, resource_uri,
                wsman_options_get_invoke_method(),
                wsman_options_get_properties(), options);
        if (doc) {
            wsman_output(doc);
			ws_xml_destroy_doc(doc);
        }
        break;
    case  ACTION_TRANSFER_CREATE:
        doc = cl->ft->create(cl, resource_uri,
                wsman_options_get_properties(), options);
        if (doc) {
            wsman_output(doc);
			ws_xml_destroy_doc(doc);
        }
        break;
    case  ACTION_TRANSFER_PUT:
        doc = cl->ft->put(cl, resource_uri,
                wsman_options_get_properties(), options);
        if (doc) {
            wsman_output(doc);
			ws_xml_destroy_doc(doc);
        }
        break;
    case  ACTION_TRANSFER_GET:
        doc = cl->ft->get(cl, resource_uri, options);
        if (doc) {
            wsman_output(doc);
			ws_xml_destroy_doc(doc);
        }		
        break;
    case ACTION_ENUMERATION:

        enumeration_mode = wsman_options_get_enum_mode();
        binding_enumeration_mode = wsman_options_get_binding_enum_mode();

        if (enumeration_mode) {
            if (strcmp(enumeration_mode, "epr") == 0 ) 
                options.flags |= FLAG_ENUMERATION_ENUM_EPR;
            else
                options.flags |= FLAG_ENUMERATION_ENUM_OBJ_AND_EPR;
        }

        if (binding_enumeration_mode) {
            if (strcmp(binding_enumeration_mode, "include") == 0 ) 
                options.flags |= FLAG_IncludeSubClassProperties;
            else if (strcmp(binding_enumeration_mode, "exclude") == 0 ) 
                options.flags |= FLAG_ExcludeSubClassProperties;
            else if (strcmp(binding_enumeration_mode, "none") == 0 ) 
                options.flags |= FLAG_POLYMORPHISM_NONE;
        }

        if (wsman_options_get_optimize_enum()) 
		{
            options.flags |= FLAG_ENUMERATION_OPTIMIZATION;
            optimize_max_elements = wsman_options_get_max_elements();
        }

        if (wsman_options_get_estimate_enum())
            options.flags |= FLAG_ENUMERATION_COUNT_ESTIMATION;

        WsXmlDocH enum_response = cl->ft->wsenum_enumerate(cl,
                resource_uri, optimize_max_elements, options);

        enumContext = wsenum_get_enum_context(enum_response); 

        WsXmlNodeH cntxNode;
        debug( "enumContext: %s", enumContext );
        if (enumContext) 
		{
            while( (doc = cl->ft->wsenum_pull(cl, resource_uri, enumContext,
                                wsman_options_get_max_elements(), options) )) 
			{
                WsXmlNodeH node = ws_xml_get_child(ws_xml_get_soap_body(doc),
                        0, NULL, NULL);
                wsman_output(doc);
                if ( strcmp(ws_xml_get_node_local_name(node),
                                                WSENUM_PULL_RESP) != 0 ) {
                    error( "no Pull response" );
					if (doc)
				        ws_xml_destroy_doc(doc);
                    break;
                }
                if ( ws_xml_get_child(node, 0, XML_NS_ENUMERATION,
                                                WSENUM_END_OF_SEQUENCE) ) {
                    debug( "End of sequence");
					/* FIXME */
					/*
					if (doc)
				        ws_xml_destroy_doc(doc);
					*/
                    break;
                }
                if ( (cntxNode = ws_xml_get_child(node, 0, XML_NS_ENUMERATION,
                                            WSENUM_ENUMERATION_CONTEXT)) ) 
				{
                    u_free(enumContext);
                    enumContext = u_str_clone(ws_xml_get_node_text(cntxNode));
                }
                if ( enumContext == NULL || enumContext[0] == 0 ) {
                    error( "No enumeration context");
					if (doc)
				        ws_xml_destroy_doc(doc);
                    break;
                }
				if (doc)
			        ws_xml_destroy_doc(doc);
                /*
				// Used for testing invalid enum contexts
                if (counter == 1 ) {
                    enumContext = "xxxx";
                }
                counter++;
                */
            }
        } else {
            if (enum_response) {
				wsman_output(enum_response);			
				ws_xml_destroy_doc(doc);         
			}
        }
        break;
    default:
        fprintf(stderr, "Action not supported\n");
        retVal = 1;
    }
	destroy_action_options(&options);
    cl->ft->release(cl);   
	/*
    if (doc)
        ws_xml_destroy_doc(doc);
	
	if (cntx)
		soap_destroy_fw((SoapH )cntx);
	*/

    if (ini)
        iniparser_freedict(ini);

#ifdef DEBUG_VERBOSE
    printf("     ******   Transfer Time = %ull usecs ******\n", get_transfer_time());
#endif
    return retVal;

}
