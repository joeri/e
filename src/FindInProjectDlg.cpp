/*******************************************************************************
 *
 * Copyright (C) 2009, Alexander Stigsen, e-texteditor.com
 *
 * This software is licensed under the Open Company License as described
 * in the file license.txt, which you should have received as part of this
 * distribution. The terms are also available at http://opencompany.org/license.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ******************************************************************************/

#include "FindInProjectDlg.h"
#include "MMapBuffer.h"
#include "EditorFrame.h"
#include "ProjectInfoHandler.h"

#ifdef __WXMSW__
    #include "IEHtmlWin.h"
#elif defined __WXGTK__
    #include "WebKitHtmlWnd.h"
#endif

// Ctrl id's
enum {
	CTRL_SEARCH,
	CTRL_SEARCHBUTTON,
	CTRL_BROWSER
};

BEGIN_EVENT_TABLE(FindInProjectDlg, wxDialog)
	EVT_TEXT_ENTER(CTRL_SEARCH, FindInProjectDlg::OnSearch)
	EVT_BUTTON(CTRL_SEARCHBUTTON, FindInProjectDlg::OnSearch)
	EVT_IDLE(FindInProjectDlg::OnIdle)
	EVT_CLOSE(FindInProjectDlg::OnClose) 
	EVT_HTMLWND_BEFORE_LOAD(CTRL_BROWSER, FindInProjectDlg::OnBeforeLoad)
END_EVENT_TABLE()

FindInProjectDlg::FindInProjectDlg(EditorFrame& parentFrame, const ProjectInfoHandler& projectPane)
: wxDialog (&parentFrame, -1, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER),
  m_parentFrame(parentFrame), m_projectPane(projectPane), m_searchThread(NULL)
{
	SetTitle (_("Find In Project"));

	// Create the search thread
	m_searchThread = new SearchThread();
	
	// Create ctrls
	m_searchCtrl = new wxTextCtrl(this, CTRL_SEARCH, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	m_searchButton = new wxButton(this, CTRL_SEARCHBUTTON, _("Search"));
	m_caseCheck = new wxCheckBox(this, wxID_ANY, wxT("Match case"));
	m_caseCheck->SetValue(true); // default is to match case
	m_pathStatic = new wxStaticText(this, wxID_ANY, wxT(""));

#if defined (__WXMSW__)
	m_browser = new wxIEHtmlWin(this, CTRL_BROWSER); // IE Control
#elif defined (__WXGTK__)
	m_browser = new wxBrowser(this, CTRL_BROWSER); // WebKit control
#endif

	// Create Layout
	wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);
		wxBoxSizer *searchSizer = new wxBoxSizer(wxHORIZONTAL);
			searchSizer->Add(m_searchCtrl, 1, wxEXPAND|wxRIGHT, 5);
			searchSizer->Add(m_searchButton, 0);
			mainSizer->Add(searchSizer, 0, wxEXPAND|wxALL, 5);
		mainSizer->Add(m_caseCheck, 0, wxLEFT, 5);
		mainSizer->Add(m_pathStatic, 0, wxEXPAND|wxALL, 5);
		mainSizer->Add(m_browser->GetWindow(), 1, wxEXPAND);
		
	SetSizer(mainSizer);
	SetSize(700, 500);
	Centre();
}

FindInProjectDlg::~FindInProjectDlg() {
	if (m_searchThread->IsSearching()) m_searchThread->CancelSearch();
	m_searchThread->DeleteThread();
}
	

void FindInProjectDlg::OnSearch(wxCommandEvent& WXUNUSED(event)) {
	if (!m_parentFrame.HasProject()) return;

	if (m_searchThread->IsSearching()) {
		m_searchThread->CancelSearch();
		m_searchButton->SetLabel(_("Search"));
		m_pathStatic->SetLabel(wxT(""));
		return;
	}

	// Clear old results
	m_output.clear();
	m_browser->LoadString(wxT(""));

	const wxString searchtext = m_searchCtrl->GetValue();
	if (searchtext.empty()) return;

	const wxFileName& projectDir = m_projectPane.GetRoot();

	// There might be a running search that we have to cancel first
	m_searchThread->CancelSearch();

	wxLogDebug(wxT("Searching:"));
	const wxString path = projectDir.GetPath() + wxFILE_SEP_PATH;
	m_searchThread->StartSearch(path, searchtext, m_caseCheck->GetValue());

	m_searchButton->SetLabel(_("Cancel"));
}

void FindInProjectDlg::OnIdle(wxIdleEvent& event) {
	if (!m_searchThread->IsSearching()) {
		if (!m_pathStatic->GetLabel().empty()){
			m_pathStatic->SetLabel(wxT(""));
			m_searchButton->SetLabel(_("Search"));
		}
		return;
	}

	wxString currentPath = m_pathStatic->GetLabel();
	if (m_searchThread->GetCurrentPath(currentPath)) m_pathStatic->SetLabel(currentPath);

	if (m_searchThread->UpdateOutput(m_output)) {
		m_browser->LoadString(m_output);
	}

	event.RequestMore(); // we don't want the seach to look slow :-)
}

void FindInProjectDlg::OnClose(wxCloseEvent& event) {
	Hide();
	if (m_searchThread->IsSearching()) m_searchThread->CancelSearch();

	event.Skip();
}

void FindInProjectDlg::OnBeforeLoad(IHtmlWndBeforeLoadEvent& event) {
    const wxString url = event.GetURL();
	if (url.StartsWith(wxT("txmt://open"))) {
		m_parentFrame.OpenTxmtUrl(url);

		// Don't try to open it in browser
		event.Cancel(true);
		return;
	}
}

// ---- SearchThread ---------------------------------------------------------------------------------------

FindInProjectDlg::SearchThread::SearchThread()
: m_isSearching(false), m_isWaiting(false), m_stopSearch(false), m_startSearchCond(m_condMutex) {
	// Create and run the thread
	Create();
	Run();
}

void* FindInProjectDlg::SearchThread::Entry() {
	while (1) {
		// Wait for signal that we should start search
		m_isWaiting = true;
		wxMutexLocker lock(m_condMutex);
		m_startSearchCond.Wait();
		m_isWaiting = false;

		if (m_stopSearch) {
			while (1) {
				if (TestDestroy()) break;
				Sleep(100);
			}
		}

		m_isSearching = true;

		SearchInfo si;
		PrepareSearchInfo(si, m_pattern, m_matchCase);
		
		ProjectInfoHandler infoHandler;
		infoHandler.SetRoot(m_path);

		// Write the html header for output
		m_outputCrit.Enter();
		m_output = wxT("<head><style type=\"text/css\">#match {background-color: yellow}</style></head>");
		m_outputCrit.Leave();

		SearchDir(m_path, si, infoHandler);
		m_isSearching = false;
	}

	return NULL;
}

void FindInProjectDlg::SearchThread::DeleteThread() {
	// We may be waiting for the condition so we cannot just call Delete
	m_stopSearch = true;
	CancelSearch();

	// Signal thread to stop waiting
	wxMutexLocker lock(m_condMutex);
	m_startSearchCond.Signal();
}

void FindInProjectDlg::SearchThread::StartSearch(const wxString& path, const wxString& pattern, bool matchCase) {
	m_path = path;
	m_pattern = pattern.c_str(); // wxString is not threadsafe, so we have to force copy
	m_matchCase = matchCase;

	// Signal thread that we should start search
	wxMutexLocker lock(m_condMutex);
	m_startSearchCond.Signal();
}

void FindInProjectDlg::SearchThread::CancelSearch() {
	m_isSearching = false;

	// Wait for the search to actually cancel
	while (!m_isWaiting) wxSleep(1);
}

bool FindInProjectDlg::SearchThread::GetCurrentPath(wxString& currentPath) {
	wxCriticalSectionLocker locker(m_outputCrit);

	if (m_currentPath != currentPath) {
		currentPath = m_currentPath.c_str(); // wxString is not threadsafe, so we have to force copy
		return true;
	}

	return false;
}

bool FindInProjectDlg::SearchThread::UpdateOutput(wxString& output) {
	wxCriticalSectionLocker locker(m_outputCrit);

	if (m_output.length() > output.length()) {
		output = m_output.c_str(); // wxString is not threadsafe, so we have to force copy
		return true;
	}

	return false;
}

void FindInProjectDlg::SearchThread::PrepareSearchInfo(SearchInfo& si, const wxString& pattern, bool matchCase) const {
	si.pattern = pattern;
	si.matchCase = matchCase;

	// We need both upper- & lowercase versions for caseless search
	if (!matchCase) {
		si.pattern.MakeLower();
		si.patternUpper = pattern;
		si.patternUpper.MakeUpper();
	}

	// Convert the searchtext to UTF8
	// For performance we assume all text to be in utf8 format.
	// In the future we might want to detect encoding of each file
	// before searching it, but it will cost us some speed.
	si.UTF8buffer = wxConvUTF8.cWC2MB(si.pattern);
	si.byte_len = strlen(si.UTF8buffer);
	if (!matchCase) {
		si.UTF8bufferUpper = wxConvUTF8.cWC2MB(si.patternUpper);

		// This algorithm assumes that UTF8 upper- & lowercase chars have same byte width
		wxASSERT(si.byte_len == strlen(si.UTF8bufferUpper));
	}

	// Build a dictionary of char-to-last distances in the search string
	// since this is a char, distances in the searchstring can not be longer than 256
	memset(si.charmap, si.byte_len, 256);
	const size_t last_char_pos = si.byte_len-1;
	for (size_t i = 0; i < last_char_pos; ++i) {
		si.charmap[si.UTF8buffer[i]] = last_char_pos-i;
		if (!matchCase) si.charmap[si.UTF8bufferUpper[i]] = last_char_pos-i;
	}
}

void FindInProjectDlg::SearchThread::SearchDir(const wxString& path, const SearchInfo& si, ProjectInfoHandler& infoHandler) {
	MMapBuffer buf;
	wxFileName filepath;
	vector<FileMatch> matches;

	wxArrayString dirs;
	wxArrayString filenames;
	infoHandler.GetDirAndFileLists(path, dirs, filenames);

	for (size_t f = 0; f < filenames.size(); ++f) {
		if (!m_isSearching) return;
		m_outputCrit.Enter();
			m_currentPath = path + filenames[f];
		m_outputCrit.Leave();
		filepath = m_currentPath;
	
		// Map the file to memory
		buf.Open(filepath);
		if (!buf.IsMapped()) {
			wxLogDebug(wxT(" Mapping failed!"));
			continue;
		}

		// Search the file
		DoSearch(buf, si, matches);
		if (matches.empty()) continue; 

		// Show matches
		WriteResult(buf, filepath, matches);
		matches.clear();
	}

	for (size_t d = 0; d < dirs.size(); ++d) {
		const wxString dirpath = path + dirs[d] + wxFILE_SEP_PATH;
		SearchDir(dirpath, si, infoHandler);
	}
}

void FindInProjectDlg::SearchThread::DoSearch(const MMapBuffer& buf, const SearchInfo& si, vector<FileMatch>& matches) const {
	// Ignore binary files (we just check for zero bytes in the first
	// 100 bytes of the file)
	const wxFileOffset len = buf.Length();
	const char* subject = buf.data();
	const char* end_pos = buf.data() + wxMin(100,len);
	for (; subject < end_pos; ++subject) {
		if (*subject == '\0') return;
	}

	// Prepare vars to avoid lookups in loop
	const size_t last_char_pos = si.byte_len-1;
	const char lastChar = si.UTF8buffer[last_char_pos];
	const char lastCharUpper = si.matchCase ? '\0' : si.UTF8bufferUpper[last_char_pos];

	subject = buf.data() + last_char_pos;
	end_pos = buf.data() + len;

	while (subject < end_pos) {
		const char c = *subject; // Get candidate for last char

		if (c == lastChar || (!si.matchCase && c == lastCharUpper)) {
			// Match indiviual chars
			const char* byte_ptr = subject-1;
			const char* const first_byte_pos = subject - last_char_pos;
			unsigned int char_pos = last_char_pos-1;
			while (byte_ptr >= first_byte_pos) {
				const char c2 = *byte_ptr;
				if (c2 != si.UTF8buffer[char_pos]) {
					if (si.matchCase || c2 != si.UTF8bufferUpper[char_pos]) break;
				}
				--byte_ptr; --char_pos;
			}

			if (byte_ptr < first_byte_pos) {
				// We got a match
				const wxFileOffset matchStart = first_byte_pos - buf.data();
				const FileMatch m = {0, 0, matchStart, matchStart + si.byte_len};
				matches.push_back(m);

				subject += si.byte_len;
				continue;
			}
		}

		// If we don't have a match, see how far we can move char_pos
		subject += si.charmap[(unsigned char)c];
	}
}

void FindInProjectDlg::SearchThread::WriteResult(const MMapBuffer& buf, const wxFileName& filepath, vector<FileMatch>& matches) {
	if (matches.empty()) return;

	// Header
	const wxString path = filepath.GetFullPath();
	const wxString format = (matches.size() == 1) ? _("<b>%s - %d match</b>") : _("<b>%s - %d matches</b>");
	wxString output = wxString::Format(format, path.c_str(), matches.size());
	output += wxT("<br><table cellspacing=0>");

	unsigned int linecount = 1;
	const char* subject = buf.data();
	const char* end = subject + buf.Length();
	const char* linestart = subject;
	vector<FileMatch>::iterator m = matches.begin();
	const char* matchstart = subject + m->start;

	// Count lines
	while (subject < end) {
		if (subject == matchstart) {
			// Write linenumber with link
			wxString line = wxString::Format(wxT("<tr><td bgcolor=#f6f6ef align=\"right\"><a href=\"txmt://open&url=file://%s&line=%d\">%d</a></td><td> "), path.c_str(), linecount, linecount);
			
			// Start of line
			line += wxString(linestart, wxConvUTF8, matchstart-linestart);

			// Match
			line += wxT("<i style=\"background-color: yellow\">");
			const size_t match_len = m->end - m->start;
			line += wxString(matchstart, wxConvUTF8, match_len);
			line += wxT("</i>");

			// End of line
			const char* const matchend = subject + match_len;
			const char* lineend = matchend;
			while (lineend < end && *lineend != '\n') ++lineend;
			line += wxString(matchend, wxConvUTF8, lineend - matchend);
			line += wxT("</td></tr>");
			output += line;
			
			++m;
			if (m == matches.end()) break;
			matchstart = buf.data() + m->start;
		}

		if (*subject == '\n') {
			++linecount;
			linestart = subject+1;
		}

		++subject;
	}

	output += wxT("</table><p>");

	m_outputCrit.Enter();
		m_output += output;
	m_outputCrit.Leave();
}
