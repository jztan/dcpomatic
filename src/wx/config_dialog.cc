/*
    Copyright (C) 2012-2014 Carl Hetherington <cth@carlh.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

/** @file src/config_dialog.cc
 *  @brief A dialogue to edit DCP-o-matic configuration.
 */

#include <iostream>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>
#include <wx/stdpaths.h>
#include <wx/preferences.h>
#include <wx/filepicker.h>
#include <wx/spinctrl.h>
#include <libdcp/colour_matrix.h>
#include "lib/config.h"
#include "lib/ratio.h"
#include "lib/scaler.h"
#include "lib/filter.h"
#include "lib/dcp_content_type.h"
#include "lib/colour_conversion.h"
#include "config_dialog.h"
#include "wx_util.h"
#include "editable_list.h"
#include "filter_dialog.h"
#include "dir_picker_ctrl.h"
#include "dci_metadata_dialog.h"
#include "preset_colour_conversion_dialog.h"
#include "server_dialog.h"

using std::vector;
using std::string;
using std::list;
using std::cout;
using boost::bind;
using boost::shared_ptr;
using boost::lexical_cast;

class GeneralPage : public wxStockPreferencesPage
{
public:
	GeneralPage ()
		: wxStockPreferencesPage (Kind_General)
	{}

	wxWindow* CreateWindow (wxWindow* parent)
	{
		wxPanel* panel = new wxPanel (parent);
		wxBoxSizer* s = new wxBoxSizer (wxVERTICAL);
		panel->SetSizer (s);

		wxFlexGridSizer* table = new wxFlexGridSizer (2, DCPOMATIC_SIZER_X_GAP, DCPOMATIC_SIZER_Y_GAP);
		table->AddGrowableCol (1, 1);
		s->Add (table, 1, wxALL | wxEXPAND, 8);
		
		_set_language = new wxCheckBox (panel, wxID_ANY, _("Set language"));
		table->Add (_set_language, 1);
		_language = new wxChoice (panel, wxID_ANY);
		_language->Append (wxT ("English"));
		_language->Append (wxT ("Français"));
		_language->Append (wxT ("Italiano"));
		_language->Append (wxT ("Español"));
		_language->Append (wxT ("Svenska"));
		_language->Append (wxT ("Deutsch"));
		table->Add (_language);
		
		wxStaticText* restart = add_label_to_sizer (table, panel, _("(restart DCP-o-matic to see language changes)"), false);
		wxFont font = restart->GetFont();
		font.SetStyle (wxFONTSTYLE_ITALIC);
		font.SetPointSize (font.GetPointSize() - 1);
		restart->SetFont (font);
		table->AddSpacer (0);
		
		add_label_to_sizer (table, panel, _("Threads to use for encoding on this host"), true);
		_num_local_encoding_threads = new wxSpinCtrl (panel);
		table->Add (_num_local_encoding_threads, 1);
		
		add_label_to_sizer (table, panel, _("Outgoing mail server"), true);
		_mail_server = new wxTextCtrl (panel, wxID_ANY);
		table->Add (_mail_server, 1, wxEXPAND | wxALL);
		
		add_label_to_sizer (table, panel, _("Mail user name"), true);
		_mail_user = new wxTextCtrl (panel, wxID_ANY);
		table->Add (_mail_user, 1, wxEXPAND | wxALL);
		
		add_label_to_sizer (table, panel, _("Mail password"), true);
		_mail_password = new wxTextCtrl (panel, wxID_ANY);
		table->Add (_mail_password, 1, wxEXPAND | wxALL);
		
		wxStaticText* plain = add_label_to_sizer (table, panel, _("(password will be stored on disk in plaintext)"), false);
		plain->SetFont (font);
		table->AddSpacer (0);
		
		add_label_to_sizer (table, panel, _("From address for KDM emails"), true);
		_kdm_from = new wxTextCtrl (panel, wxID_ANY);
		table->Add (_kdm_from, 1, wxEXPAND | wxALL);
		
		_check_for_updates = new wxCheckBox (panel, wxID_ANY, _("Check for updates on startup"));
		table->Add (_check_for_updates, 1, wxEXPAND | wxALL);
		table->AddSpacer (0);
		
		_check_for_test_updates = new wxCheckBox (panel, wxID_ANY, _("Check for testing updates as well as stable ones"));
		table->Add (_check_for_test_updates, 1, wxEXPAND | wxALL);
		table->AddSpacer (0);
		
		Config* config = Config::instance ();
		
		_set_language->SetValue (config->language ());
		
		if (config->language().get_value_or ("") == "fr") {
			_language->SetSelection (1);
		} else if (config->language().get_value_or ("") == "it") {
		_language->SetSelection (2);
		} else if (config->language().get_value_or ("") == "es") {
			_language->SetSelection (3);
		} else if (config->language().get_value_or ("") == "sv") {
			_language->SetSelection (4);
		} else if (config->language().get_value_or ("") == "de") {
			_language->SetSelection (5);
		} else {
			_language->SetSelection (0);
		}
		
		setup_language_sensitivity ();
		
		_set_language->Bind (wxEVT_COMMAND_CHECKBOX_CLICKED, boost::bind (&GeneralPage::set_language_changed, this));
		_language->Bind     (wxEVT_COMMAND_CHOICE_SELECTED,  boost::bind (&GeneralPage::language_changed,     this));
		
		_num_local_encoding_threads->SetRange (1, 128);
		_num_local_encoding_threads->SetValue (config->num_local_encoding_threads ());
		_num_local_encoding_threads->Bind (wxEVT_COMMAND_SPINCTRL_UPDATED, boost::bind (&GeneralPage::num_local_encoding_threads_changed, this));
		
		_mail_server->SetValue (std_to_wx (config->mail_server ()));
		_mail_server->Bind (wxEVT_COMMAND_TEXT_UPDATED, boost::bind (&GeneralPage::mail_server_changed, this));
		_mail_user->SetValue (std_to_wx (config->mail_user ()));
		_mail_user->Bind (wxEVT_COMMAND_TEXT_UPDATED, boost::bind (&GeneralPage::mail_user_changed, this));
		_mail_password->SetValue (std_to_wx (config->mail_password ()));
		_mail_password->Bind (wxEVT_COMMAND_TEXT_UPDATED, boost::bind (&GeneralPage::mail_password_changed, this));
		_kdm_from->SetValue (std_to_wx (config->kdm_from ()));
		_kdm_from->Bind (wxEVT_COMMAND_TEXT_UPDATED, boost::bind (&GeneralPage::kdm_from_changed, this));
		_check_for_updates->SetValue (config->check_for_updates ());
		_check_for_updates->Bind (wxEVT_COMMAND_CHECKBOX_CLICKED, boost::bind (&GeneralPage::check_for_updates_changed, this));
		_check_for_test_updates->SetValue (config->check_for_test_updates ());
		_check_for_test_updates->Bind (wxEVT_COMMAND_CHECKBOX_CLICKED, boost::bind (&GeneralPage::check_for_test_updates_changed, this));
		
		return panel;
	}

private:	
	void setup_language_sensitivity ()
	{
		_language->Enable (_set_language->GetValue ());
	}

	void set_language_changed ()
	{
		setup_language_sensitivity ();
		if (_set_language->GetValue ()) {
			language_changed ();
		} else {
			Config::instance()->unset_language ();
		}
	}

	void language_changed ()
	{
		switch (_language->GetSelection ()) {
		case 0:
			Config::instance()->set_language ("en");
			break;
		case 1:
			Config::instance()->set_language ("fr");
			break;
		case 2:
			Config::instance()->set_language ("it");
			break;
		case 3:
			Config::instance()->set_language ("es");
			break;
		case 4:
			Config::instance()->set_language ("sv");
			break;
		case 5:
			Config::instance()->set_language ("de");
			break;
		}
	}
	
	void mail_server_changed ()
	{
		Config::instance()->set_mail_server (wx_to_std (_mail_server->GetValue ()));
	}
	
	void mail_user_changed ()
	{
		Config::instance()->set_mail_user (wx_to_std (_mail_user->GetValue ()));
	}
	
	void mail_password_changed ()
	{
		Config::instance()->set_mail_password (wx_to_std (_mail_password->GetValue ()));
	}
	
	void kdm_from_changed ()
	{
		Config::instance()->set_kdm_from (wx_to_std (_kdm_from->GetValue ()));
	}

	void check_for_updates_changed ()
	{
		Config::instance()->set_check_for_updates (_check_for_updates->GetValue ());
	}
	
	void check_for_test_updates_changed ()
	{
		Config::instance()->set_check_for_test_updates (_check_for_test_updates->GetValue ());
	}

	void num_local_encoding_threads_changed ()
	{
		Config::instance()->set_num_local_encoding_threads (_num_local_encoding_threads->GetValue ());
	}
	
	wxCheckBox* _set_language;
	wxChoice* _language;
	wxSpinCtrl* _num_local_encoding_threads;
	wxTextCtrl* _mail_server;
	wxTextCtrl* _mail_user;
	wxTextCtrl* _mail_password;
	wxTextCtrl* _kdm_from;
	wxCheckBox* _check_for_updates;
	wxCheckBox* _check_for_test_updates;
};

class DefaultsPage : public wxPreferencesPage
{
public:
	wxString GetName () const
	{
		return _("Defaults");
	}

#ifdef DCPOMATIC_OSX	
	wxBitmap GetLargeIcon () const
	{
		return wxBitmap ("defaults", wxBITMAP_TYPE_PNG_RESOURCE);
	}
#endif	

	wxWindow* CreateWindow (wxWindow* parent)
	{
		wxPanel* panel = new wxPanel (parent);
		wxBoxSizer* s = new wxBoxSizer (wxVERTICAL);
		panel->SetSizer (s);

		wxFlexGridSizer* table = new wxFlexGridSizer (2, DCPOMATIC_SIZER_X_GAP, DCPOMATIC_SIZER_Y_GAP);
		table->AddGrowableCol (1, 1);
		s->Add (table, 1, wxALL | wxEXPAND, 8);
		
		{
			add_label_to_sizer (table, panel, _("Default duration of still images"), true);
			wxBoxSizer* s = new wxBoxSizer (wxHORIZONTAL);
			_default_still_length = new wxSpinCtrl (panel);
			s->Add (_default_still_length);
			add_label_to_sizer (s, panel, _("s"), false);
			table->Add (s, 1);
		}
		
		add_label_to_sizer (table, panel, _("Default directory for new films"), true);
#ifdef DCPOMATIC_USE_OWN_DIR_PICKER
		_default_directory = new DirPickerCtrl (panel);
#else	
		_default_directory = new wxDirPickerCtrl (panel, wxDD_DIR_MUST_EXIST);
#endif
		table->Add (_default_directory, 1, wxEXPAND);
		
		add_label_to_sizer (table, panel, _("Default DCI name details"), true);
		_default_dci_metadata_button = new wxButton (panel, wxID_ANY, _("Edit..."));
		table->Add (_default_dci_metadata_button);
		
		add_label_to_sizer (table, panel, _("Default container"), true);
		_default_container = new wxChoice (panel, wxID_ANY);
		table->Add (_default_container);
		
		add_label_to_sizer (table, panel, _("Default content type"), true);
		_default_dcp_content_type = new wxChoice (panel, wxID_ANY);
		table->Add (_default_dcp_content_type);
		
		{
			add_label_to_sizer (table, panel, _("Default JPEG2000 bandwidth"), true);
			wxBoxSizer* s = new wxBoxSizer (wxHORIZONTAL);
			_default_j2k_bandwidth = new wxSpinCtrl (panel);
			s->Add (_default_j2k_bandwidth);
			add_label_to_sizer (s, panel, _("Mbit/s"), false);
			table->Add (s, 1);
		}
		
		{
			add_label_to_sizer (table, panel, _("Default audio delay"), true);
			wxBoxSizer* s = new wxBoxSizer (wxHORIZONTAL);
			_default_audio_delay = new wxSpinCtrl (panel);
			s->Add (_default_audio_delay);
			add_label_to_sizer (s, panel, _("ms"), false);
			table->Add (s, 1);
		}
		
		Config* config = Config::instance ();
		
		_default_still_length->SetRange (1, 3600);
		_default_still_length->SetValue (config->default_still_length ());
		_default_still_length->Bind (wxEVT_COMMAND_SPINCTRL_UPDATED, boost::bind (&DefaultsPage::default_still_length_changed, this));
		
		_default_directory->SetPath (std_to_wx (config->default_directory_or (wx_to_std (wxStandardPaths::Get().GetDocumentsDir())).string ()));
		_default_directory->Bind (wxEVT_COMMAND_DIRPICKER_CHANGED, boost::bind (&DefaultsPage::default_directory_changed, this));
		
		_default_dci_metadata_button->Bind (wxEVT_COMMAND_BUTTON_CLICKED, boost::bind (&DefaultsPage::edit_default_dci_metadata_clicked, this, parent));
		
		vector<Ratio const *> ratio = Ratio::all ();
		int n = 0;
		for (vector<Ratio const *>::iterator i = ratio.begin(); i != ratio.end(); ++i) {
			_default_container->Append (std_to_wx ((*i)->nickname ()));
			if (*i == config->default_container ()) {
				_default_container->SetSelection (n);
			}
			++n;
		}
		
		_default_container->Bind (wxEVT_COMMAND_CHOICE_SELECTED, boost::bind (&DefaultsPage::default_container_changed, this));
		
		vector<DCPContentType const *> const ct = DCPContentType::all ();
		n = 0;
		for (vector<DCPContentType const *>::const_iterator i = ct.begin(); i != ct.end(); ++i) {
			_default_dcp_content_type->Append (std_to_wx ((*i)->pretty_name ()));
			if (*i == config->default_dcp_content_type ()) {
				_default_dcp_content_type->SetSelection (n);
			}
			++n;
		}
		
		_default_dcp_content_type->Bind (wxEVT_COMMAND_CHOICE_SELECTED, boost::bind (&DefaultsPage::default_dcp_content_type_changed, this));
		
		_default_j2k_bandwidth->SetRange (50, 250);
		_default_j2k_bandwidth->SetValue (config->default_j2k_bandwidth() / 1000000);
		_default_j2k_bandwidth->Bind (wxEVT_COMMAND_SPINCTRL_UPDATED, boost::bind (&DefaultsPage::default_j2k_bandwidth_changed, this));
		
		_default_audio_delay->SetRange (-1000, 1000);
		_default_audio_delay->SetValue (config->default_audio_delay ());
		_default_audio_delay->Bind (wxEVT_COMMAND_SPINCTRL_UPDATED, boost::bind (&DefaultsPage::default_audio_delay_changed, this));

		return panel;
	}

private:
	void default_j2k_bandwidth_changed ()
	{
		Config::instance()->set_default_j2k_bandwidth (_default_j2k_bandwidth->GetValue() * 1000000);
	}
	
	void default_audio_delay_changed ()
	{
		Config::instance()->set_default_audio_delay (_default_audio_delay->GetValue());
	}

	void default_directory_changed ()
	{
		Config::instance()->set_default_directory (wx_to_std (_default_directory->GetPath ()));
	}

	void edit_default_dci_metadata_clicked (wxWindow* parent)
	{
		DCIMetadataDialog* d = new DCIMetadataDialog (parent, Config::instance()->default_dci_metadata ());
		d->ShowModal ();
		Config::instance()->set_default_dci_metadata (d->dci_metadata ());
		d->Destroy ();
	}

	void default_still_length_changed ()
	{
		Config::instance()->set_default_still_length (_default_still_length->GetValue ());
	}
	
	void default_container_changed ()
	{
		vector<Ratio const *> ratio = Ratio::all ();
		Config::instance()->set_default_container (ratio[_default_container->GetSelection()]);
	}
	
	void default_dcp_content_type_changed ()
	{
		vector<DCPContentType const *> ct = DCPContentType::all ();
		Config::instance()->set_default_dcp_content_type (ct[_default_dcp_content_type->GetSelection()]);
	}
	
	wxSpinCtrl* _default_j2k_bandwidth;
	wxSpinCtrl* _default_audio_delay;
	wxButton* _default_dci_metadata_button;
	wxSpinCtrl* _default_still_length;
#ifdef DCPOMATIC_USE_OWN_DIR_PICKER
	DirPickerCtrl* _default_directory;
#else
	wxDirPickerCtrl* _default_directory;
#endif
	wxChoice* _default_container;
	wxChoice* _default_dcp_content_type;
};

class EncodingServersPage : public wxPreferencesPage
{
public:
	wxString GetName () const
	{
		return _("Encoding Servers");
	}

#ifdef DCPOMATIC_OSX	
	wxBitmap GetLargeIcon () const
	{
		return wxBitmap ("blank", wxBITMAP_TYPE_PNG_RESOURCE);
	}
#endif	

	wxWindow* CreateWindow (wxWindow* parent)
	{
		wxPanel* panel = new wxPanel (parent);
		wxBoxSizer* s = new wxBoxSizer (wxVERTICAL);
		panel->SetSizer (s);
		
		_use_any_servers = new wxCheckBox (panel, wxID_ANY, _("Use all servers"));
		s->Add (_use_any_servers, 0, wxALL, DCPOMATIC_SIZER_X_GAP);
		
		vector<string> columns;
		columns.push_back (wx_to_std (_("IP address / host name")));
		_servers_list = new EditableList<string, ServerDialog> (
			panel,
			columns,
			boost::bind (&Config::servers, Config::instance()),
			boost::bind (&Config::set_servers, Config::instance(), _1),
			boost::bind (&EncodingServersPage::server_column, this, _1)
			);
		
		s->Add (_servers_list, 1, wxEXPAND | wxALL, DCPOMATIC_SIZER_X_GAP);
		
		_use_any_servers->SetValue (Config::instance()->use_any_servers ());
		_use_any_servers->Bind (wxEVT_COMMAND_CHECKBOX_CLICKED, boost::bind (&EncodingServersPage::use_any_servers_changed, this));

		return panel;
	}

private:	

	void use_any_servers_changed ()
	{
		Config::instance()->set_use_any_servers (_use_any_servers->GetValue ());
	}

	string server_column (string s)
	{
		return s;
	}

	wxCheckBox* _use_any_servers;
	EditableList<string, ServerDialog>* _servers_list;
};

class ColourConversionsPage : public wxPreferencesPage
{
	wxString GetName () const
	{
		return _("Colour Conversions");
	}

#ifdef DCPOMATIC_OSX	
	wxBitmap GetLargeIcon () const
	{
		return wxBitmap ("blank", wxBITMAP_TYPE_PNG_RESOURCE);
	}
#endif	
	wxWindow* CreateWindow (wxWindow* parent)
	{
		vector<string> columns;
		columns.push_back (wx_to_std (_("Name")));
		return new EditableList<PresetColourConversion, PresetColourConversionDialog> (
			parent,
			columns,
			boost::bind (&Config::colour_conversions, Config::instance()),
			boost::bind (&Config::set_colour_conversions, Config::instance(), _1),
			boost::bind (&ColourConversionsPage::colour_conversion_column, this, _1),
			300
			);
	}

private:
	string colour_conversion_column (PresetColourConversion c)
	{
		return c.name;
	}
};

class MetadataPage : public wxPreferencesPage
{
	wxString GetName () const
	{
		return _("Metadata");
	}

#ifdef DCPOMATIC_OSX	
	wxBitmap GetLargeIcon () const
	{
		return wxBitmap ("blank", wxBITMAP_TYPE_PNG_RESOURCE);
	}
#endif	

	wxWindow* CreateWindow (wxWindow* parent)
	{
		wxPanel* panel = new wxPanel (parent);
		wxBoxSizer* s = new wxBoxSizer (wxVERTICAL);
		panel->SetSizer (s);
		
		wxFlexGridSizer* table = new wxFlexGridSizer (2, DCPOMATIC_SIZER_X_GAP, DCPOMATIC_SIZER_Y_GAP);
		table->AddGrowableCol (1, 1);
		s->Add (table, 1, wxALL | wxEXPAND, 8);
		
		add_label_to_sizer (table, panel, _("Issuer"), true);
		_issuer = new wxTextCtrl (panel, wxID_ANY);
		table->Add (_issuer, 1, wxEXPAND);
		
		add_label_to_sizer (table, panel, _("Creator"), true);
		_creator = new wxTextCtrl (panel, wxID_ANY);
		table->Add (_creator, 1, wxEXPAND);
		
		Config* config = Config::instance ();
		
		_issuer->SetValue (std_to_wx (config->dcp_metadata().issuer));
		_issuer->Bind (wxEVT_COMMAND_TEXT_UPDATED, boost::bind (&MetadataPage::issuer_changed, this));
		_creator->SetValue (std_to_wx (config->dcp_metadata().creator));
		_creator->Bind (wxEVT_COMMAND_TEXT_UPDATED, boost::bind (&MetadataPage::creator_changed, this));

		return panel;
	}		

private:
	wxTextCtrl* _issuer;
	wxTextCtrl* _creator;
	
	void issuer_changed ()
	{
		libdcp::XMLMetadata m = Config::instance()->dcp_metadata ();
		m.issuer = wx_to_std (_issuer->GetValue ());
		Config::instance()->set_dcp_metadata (m);
	}
	
	void creator_changed ()
	{
		libdcp::XMLMetadata m = Config::instance()->dcp_metadata ();
		m.creator = wx_to_std (_creator->GetValue ());
		Config::instance()->set_dcp_metadata (m);
	}
};

class TMSPage : public wxPreferencesPage
{
	wxString GetName () const
	{
		return _("TMS");
	}

#ifdef DCPOMATIC_OSX	
	wxBitmap GetLargeIcon () const
	{
		return wxBitmap ("blank", wxBITMAP_TYPE_PNG_RESOURCE);
	}
#endif	

	wxWindow* CreateWindow (wxWindow* parent)
	{
		wxPanel* panel = new wxPanel (parent);
		wxBoxSizer* s = new wxBoxSizer (wxVERTICAL);
		panel->SetSizer (s);

		wxFlexGridSizer* table = new wxFlexGridSizer (2, DCPOMATIC_SIZER_X_GAP, DCPOMATIC_SIZER_Y_GAP);
		table->AddGrowableCol (1, 1);
		s->Add (table, 1, wxALL | wxEXPAND, 8);
		
		add_label_to_sizer (table, panel, _("IP address"), true);
		_tms_ip = new wxTextCtrl (panel, wxID_ANY);
		table->Add (_tms_ip, 1, wxEXPAND);
		
		add_label_to_sizer (table, panel, _("Target path"), true);
		_tms_path = new wxTextCtrl (panel, wxID_ANY);
		table->Add (_tms_path, 1, wxEXPAND);
		
		add_label_to_sizer (table, panel, _("User name"), true);
		_tms_user = new wxTextCtrl (panel, wxID_ANY);
		table->Add (_tms_user, 1, wxEXPAND);
		
		add_label_to_sizer (table, panel, _("Password"), true);
		_tms_password = new wxTextCtrl (panel, wxID_ANY);
		table->Add (_tms_password, 1, wxEXPAND);
		
		Config* config = Config::instance ();
		
		_tms_ip->SetValue (std_to_wx (config->tms_ip ()));
		_tms_ip->Bind (wxEVT_COMMAND_TEXT_UPDATED, boost::bind (&TMSPage::tms_ip_changed, this));
		_tms_path->SetValue (std_to_wx (config->tms_path ()));
		_tms_path->Bind (wxEVT_COMMAND_TEXT_UPDATED, boost::bind (&TMSPage::tms_path_changed, this));
		_tms_user->SetValue (std_to_wx (config->tms_user ()));
		_tms_user->Bind (wxEVT_COMMAND_TEXT_UPDATED, boost::bind (&TMSPage::tms_user_changed, this));
		_tms_password->SetValue (std_to_wx (config->tms_password ()));
		_tms_password->Bind (wxEVT_COMMAND_TEXT_UPDATED, boost::bind (&TMSPage::tms_password_changed, this));

		return panel;
	}

private:
	void tms_ip_changed ()
	{
		Config::instance()->set_tms_ip (wx_to_std (_tms_ip->GetValue ()));
	}
	
	void tms_path_changed ()
	{
		Config::instance()->set_tms_path (wx_to_std (_tms_path->GetValue ()));
	}
	
	void tms_user_changed ()
	{
		Config::instance()->set_tms_user (wx_to_std (_tms_user->GetValue ()));
	}
	
	void tms_password_changed ()
	{
		Config::instance()->set_tms_password (wx_to_std (_tms_password->GetValue ()));
	}

	wxTextCtrl* _tms_ip;
	wxTextCtrl* _tms_path;
	wxTextCtrl* _tms_user;
	wxTextCtrl* _tms_password;
};

class KDMEmailPage : public wxPreferencesPage
{
public:
	wxString GetName () const
	{
		return _("KDM Email");
	}

#ifdef DCPOMATIC_OSX	
	wxBitmap GetLargeIcon () const
	{
		return wxBitmap ("blank", wxBITMAP_TYPE_PNG_RESOURCE);
	}
#endif	

	wxWindow* CreateWindow (wxWindow* parent)
	{
		wxPanel* panel = new wxPanel (parent);
		wxBoxSizer* s = new wxBoxSizer (wxVERTICAL);
		panel->SetSizer (s);
		
		_kdm_email = new wxTextCtrl (panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
		s->Add (_kdm_email, 1, wxEXPAND | wxALL, 12);
		
		_kdm_email->Bind (wxEVT_COMMAND_TEXT_UPDATED, boost::bind (&KDMEmailPage::kdm_email_changed, this));
		_kdm_email->SetValue (wx_to_std (Config::instance()->kdm_email ()));

		return panel;
	}

private:	
	void kdm_email_changed ()
	{
		Config::instance()->set_kdm_email (wx_to_std (_kdm_email->GetValue ()));
	}

	wxTextCtrl* _kdm_email;
};

wxPreferencesEditor*
create_config_dialog ()
{
	wxPreferencesEditor* e = new wxPreferencesEditor ();
	e->AddPage (new GeneralPage);
	e->AddPage (new DefaultsPage);
	e->AddPage (new EncodingServersPage);
	e->AddPage (new ColourConversionsPage);
	e->AddPage (new MetadataPage);
	e->AddPage (new TMSPage);
	e->AddPage (new KDMEmailPage);
	return e;
}
