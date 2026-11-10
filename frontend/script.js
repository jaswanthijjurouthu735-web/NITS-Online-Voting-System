"use strict";

const API = "http://localhost:18080/api";

const POSITIONS = [
  { code: "GS", name: "Gymkhana President", short: "President", symbol: "✳", desc: "Overall student representation, leadership and coordination of Gymkhana activities." },
  { code: "VP", name: "Vice President", short: "Vice President", symbol: "↗", desc: "Support student leadership, coordination and delivery of student initiatives." },
  { code: "GSS", name: "Gymkhana Sports Secretary", short: "Sports Secretary", symbol: "◎", desc: "Promote sports, athletic participation, facilities and sporting events." },
  { code: "GST", name: "Gymkhana Technical Secretary", short: "Technical Secretary", symbol: "⌘", desc: "Champion technical clubs, innovation, workshops and student projects." },
  { code: "GSC", name: "Gymkhana Cultural Secretary", short: "Cultural Secretary", symbol: "✦", desc: "Celebrate creativity through cultural festivals and student talent." },
  { code: "GR", name: "Girls' Representative", short: "Girls' Representative", symbol: "◈", desc: "Represent students' concerns and help foster an inclusive campus." }
];

const SAMPLE_CANDIDATES = [
  { id: 1, name: "Aarav Sharma", position: "GS", department: "CSE", manifesto: "Monthly student forums\nTransparent updates\nStronger club collaboration", achievements: "Coordinated student activities" },
  { id: 2, name: "Ananya Das", position: "GS", department: "ECE", manifesto: "Clearer student feedback\nMore student initiatives\nRegular progress reports", achievements: "Led student volunteer teams" },
  { id: 3, name: "Rohan Mehta", position: "VP", department: "ECE", manifesto: "Improve coordination\nShared events calendar\nTrack student feedback", achievements: "Club coordinator" },
  { id: 4, name: "Priya Sharma", position: "GSS", department: "ME", manifesto: "More intramural sports\nClear sports schedules\nImprove sports facilities", achievements: "Inter-college sports participant" },
  { id: 5, name: "Kunal Das", position: "GST", department: "CSE", manifesto: "Peer-led workshops\nClub project showcases\nCross-club hackathons", achievements: "Technical workshop volunteer" },
  { id: 6, name: "Meera Roy", position: "GSC", department: "CE", manifesto: "Open-mic events\nSupport student artists\nInclusive cultural programmes", achievements: "Cultural event volunteer" },
  { id: 7, name: "Sneha Paul", position: "GR", department: "EE", manifesto: "Accessible feedback\nCampus support awareness\nListening sessions", achievements: "Student community volunteer" }
];

const state = {
  page: "home",
  filter: "ALL",
  selected: null,
  candidates: [...SAMPLE_CANDIDATES],
  backendConnected: false,
  studentEmail: "",
  otpSent: false,
  studentToken: sessionStorage.getItem("nits_student_token") || "",
  adminToken: sessionStorage.getItem("nits_admin_token") || "",
  verificationToken: "",
  student: null,
  electionStatus: "draft",
  published: false,
  votes: {},
  results: [],
  editingId: null
};

const app = document.getElementById("app");
const toastElement = document.getElementById("toast");

const HOME_TEMPLATE = [
  "homeHero",
  ".ticker",
  ".intro-section",
  ".positions-section",
  ".gymkhana-section",
  ".cta-section"
].map(selector => {
  const element = selector.startsWith(".")
    ? document.querySelector(selector)
    : document.getElementById(selector);

  return element ? element.outerHTML : "";
}).join("");

function toast(message) {
  toastElement.textContent = message;
  toastElement.classList.add("show");

  clearTimeout(toast.timer);

  toast.timer = setTimeout(() => {
    toastElement.classList.remove("show");
  }, 2800);
}

function escapeHTML(value = "") {
  return String(value).replace(/[&<>"']/g, char => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    '"': "&quot;",
    "'": "&#039;"
  })[char]);
}

function lines(value) {
  return String(value || "")
    .split(/\r?\n/)
    .map(x => x.trim())
    .filter(Boolean);
}

function position(code) {
  return POSITIONS.find(p => p.code === code);
}

function initials(name) {
  return String(name || "")
    .trim()
    .split(/\s+/)
    .slice(0, 2)
    .map(x => x[0] || "")
    .join("")
    .toUpperCase();
}

async function apiRequest(path, options = {}) {
  const response = await fetch(`${API}${path}`, {
    ...options,
    headers: {
      ...(options.body ? { "Content-Type": "application/json" } : {}),
      ...((state.adminToken || state.studentToken) ? { "Authorization": `Bearer ${state.adminToken || state.studentToken}` } : {}),
      ...(options.headers || {})
    }
  });

  let data;

  try {
    data = await response.json();
  } catch {
    throw new Error("The backend returned an invalid response.");
  }

  if (!response.ok || data.status === "error") {
    throw new Error(data.message || `Request failed (${response.status}).`);
  }

  return data;
}

// =====================================================
// LOAD CANDIDATES
// =====================================================

async function loadCandidates() {
  try {
    const data = await apiRequest("/candidates");

    if (!Array.isArray(data.candidates)) {
      throw new Error("Candidate list is missing.");
    }

    state.candidates = data.candidates.map(c => ({
      ...c,
      id: Number(c.id),
      initials: initials(c.name),
      manifesto: String(c.manifesto || ""),
      achievements: String(c.achievements || "")
    }));

    state.backendConnected = true;

    return true;

  } catch (error) {
    state.backendConnected = false;
    console.warn("Backend unavailable:", error.message);
    return false;
  }
}

async function loadElection() {
  const data = await apiRequest("/election");
  state.electionStatus = data.election_status;
  state.published = !!data.published;
}

async function loadStudentData() {
  if (!state.studentToken) return false;
  const me = await apiRequest("/me");
  state.student = me;
  state.studentEmail = me.email;
  const votes = await apiRequest("/my-votes");
  state.votes = votes.votes || {};
  return true;
}

// Keep the portal shortcut consistent with the current signed-in role.
function updatePortalButton() {
  const button = document.querySelector(".header-login");
  if (!button) return;
  if (state.adminToken) button.innerHTML = "Admin dashboard <span>↗</span>";
  else if (state.studentToken) button.innerHTML = "Student dashboard <span>↗</span>";
  else button.innerHTML = "Enter portal <span>↗</span>";
}

async function openPortal() {
  // A signed-in user should return to their dashboard, not see the login form.
  if (state.studentToken) {
    try {
      await loadStudentData();
      await loadElection();
      setPage("student");
      return;
    } catch (error) {
      sessionStorage.removeItem("nits_student_token");
      state.studentToken = "";
      state.student = null;
      state.votes = {};
    }
  }

  if (state.adminToken) {
    // The protected admin endpoint validates the stored token without changing election state.
    try {
      await apiRequest("/admin/session");
      await loadElection();
      setPage("admin");
      return;
    } catch (error) {
      sessionStorage.removeItem("nits_admin_token");
      state.adminToken = "";
    }
  }

  updatePortalButton();
  setPage("portal");
}

// =====================================================
// NAVIGATION
// =====================================================

function setPage(page) {
  state.page = page;

  document.getElementById("mainNav").classList.remove("open");

  document.querySelectorAll(".nav-link").forEach(link => {
    link.classList.toggle("active", link.dataset.page === page);
  });

  window.scrollTo({
    top: 0,
    behavior: "smooth"
  });

  render();
}

function pageHead(kicker, title, description) {
  return `
    <div class="page-head">
      <div class="section-kicker">${escapeHTML(kicker)}</div>
      <h1>${title}</h1>
      <p>${escapeHTML(description)}</p>
    </div>`;
}

// =====================================================
// POSITION CARDS
// =====================================================

function positionCards() {
  return POSITIONS.map(p => `
    <article class="position-card"
      data-position="${p.code}"
      tabindex="0"
      role="button">

      <div class="position-card-top">
        <span class="position-code">${p.code}</span>
        <span class="position-symbol">${p.symbol}</span>
      </div>

      <h3>${escapeHTML(p.name)}</h3>

      <p>${escapeHTML(p.desc)}</p>

      <span class="card-arrow">↗</span>

    </article>
  `).join("");
}

// =====================================================
// CANDIDATE CARDS
// =====================================================

function candidateCards() {
  const filtered = state.candidates.filter(c =>
    state.filter === "ALL" || c.position === state.filter
  );

  if (!filtered.length) {
    return `
      <div class="empty-note">
        No candidates are listed for this position yet.
      </div>`;
  }

  return filtered.map(c => {
    const p = position(c.position);

    return `
      <article class="candidate-card">

        <div class="candidate-art">

          <span class="candidate-position">
            ${escapeHTML(c.position)} /
            ${escapeHTML(p?.short || c.position)}
          </span>

          <span class="candidate-avatar">
            ${escapeHTML(c.initials || initials(c.name))}
          </span>

        </div>

        <div class="candidate-info">

          <h3>${escapeHTML(c.name)}</h3>

          <p>
            ${escapeHTML(
              lines(c.manifesto)[0] ||
              "Candidate for a better campus."
            )}
          </p>

          <button class="btn btn-outline"
            data-profile="${c.id}">
            View manifesto & achievements ↗
          </button>

        </div>

      </article>`;
  }).join("");
}

// =====================================================
// HOME
// =====================================================

function renderHome() {
  app.innerHTML = HOME_TEMPLATE;

  document.getElementById("homePositionGrid").innerHTML =
    positionCards();
}

// =====================================================
// POSITIONS PAGE
// =====================================================

function renderPositions() {
  app.innerHTML = `
    <section class="page-shell">

      ${pageHead(
        "01 — THE ELECTION",
        "Know your positions.",
        "Explore the six positions and understand the responsibilities of each elected representative."
      )}

      <div class="position-grid">
        ${positionCards()}
      </div>

      <div class="warning-note">
        Confirm official position titles and eligibility against
        the current NITS Gymkhana election rules before using
        this project for a real election.
      </div>

    </section>`;
}

// =====================================================
// CANDIDATES PAGE
// =====================================================

function renderCandidates() {
  app.innerHTML = `
    <section class="page-shell">

      ${pageHead(
        "02 — MEET THE CANDIDATES",
        "People. Plans. Possibilities.",
        "Explore candidates by position. Read their manifestos and achievements before making an informed decision."
      )}

      <div class="filter-row">

        ${["ALL", ...POSITIONS.map(p => p.code)].map(code => `
          <button class="filter-chip ${
            state.filter === code ? "active" : ""
          }" data-filter="${code}">

            ${code === "ALL" ? "All positions" : code}

          </button>
        `).join("")}

      </div>

      <div class="candidate-grid">
        ${candidateCards()}
      </div>

      ${!state.backendConnected ? `
        <div class="warning-note">
          Showing sample candidates because the backend is not connected.
          Start your C++ server to load candidates from SQLite.
        </div>
      ` : ""}

    </section>`;
}

// =====================================================
// CANDIDATE PROFILE
// =====================================================

function renderProfile() {
  const c = state.candidates.find(x => x.id === state.selected);

  if (!c) {
    return setPage("candidates");
  }

  const p = position(c.position);

  app.innerHTML = `
    <section class="page-shell">

      <button class="back-link" data-page="candidates">
        ← Back to candidates
      </button>

      <div class="profile-layout">

        <aside class="profile-summary">

          <div class="candidate-art">

            <span class="candidate-avatar">
              ${escapeHTML(c.initials || initials(c.name))}
            </span>

          </div>

          <div class="section-kicker">
            ${escapeHTML(c.position)} /
            ${escapeHTML(p?.short || "")}
          </div>

          <h2>${escapeHTML(c.name)}</h2>

          <p>
            ${escapeHTML(c.department || "Department not specified")}
          </p>

        </aside>

        <div class="profile-content">

          <div class="dashboard-panel">

            <h2>Manifesto</h2>

            <ul>
              ${
                lines(c.manifesto)
                  .map(x => `<li>${escapeHTML(x)}</li>`)
                  .join("") ||
                "<li>No manifesto added.</li>"
              }
            </ul>

          </div>

          <div class="dashboard-panel">

            <h2>Achievements</h2>

            <ul>
              ${
                lines(c.achievements)
                  .map(x => `<li>${escapeHTML(x)}</li>`)
                  .join("") ||
                "<li>No achievements added.</li>"
              }
            </ul>

          </div>

        </div>

      </div>

    </section>`;
}

// =====================================================
// STUDENT PORTAL
// =====================================================

function renderPortal() {
  app.innerHTML = `
    <section class="page-shell">

      ${pageHead(
        "03 — STUDENT ACCESS",
        "Welcome to the portal.",
        "Verify your student email with a one-time password to continue."
      )}

      <div class="dashboard-grid">

        <div class="dashboard-panel">

          <h2>Student access</h2>

          <p class="form-note">
            Enter your NITS student email address to receive a six-digit OTP.
          </p>

          <form id="studentForm">

            <div class="form-field">

              <label for="studentEmail">
                NITS email address
              </label>

              <input
                id="studentEmail"
                name="email"
                type="email"
                placeholder="yourname@nits.ac.in"
                value="${escapeHTML(state.studentEmail)}"
                required
              >

            </div>

            <button
              class="btn btn-primary"
              type="submit"
              id="sendOtpButton">

              Send OTP →

            </button>

          </form>

          <form
            id="verifyOtpForm"
            style="display:${state.otpSent ? "block" : "none"}; margin-top:18px">

            <div class="form-field">

              <label for="studentOtp">
                Six-digit OTP
              </label>

              <input
                id="studentOtp"
                type="text"
                inputmode="numeric"
                maxlength="6"
                pattern="[0-9]{6}"
                placeholder="Enter OTP"
                required
              >

            </div>

            <button
              class="btn btn-primary"
              type="submit"
              id="verifyOtpButton">

              Verify OTP →

            </button>

          </form>

        </div>

        <div class="dashboard-panel">

          <h2>Administrator access</h2>

          <p class="form-note">
            Administrator access is verified by the C++ backend.
          </p>

          <form id="adminForm">

            <div class="form-field">

              <label for="adminPassword">
                Administrator password
              </label>

              <input
                id="adminPassword"
                name="password"
                type="password"
                placeholder="Enter administrator password"
                required
              >

            </div>

            <button class="btn btn-dark" type="submit">
              Enter admin dashboard →
            </button>

          </form>

        </div>

      </div>

      <div class="warning-note">
        Student registration and administrator sign-in are handled by the backend.
      </div>

    </section>`;
}

function renderRegistration() {
  app.innerHTML = `
    <section class="page-shell">
      ${pageHead("03 — STUDENT REGISTRATION", "Complete your profile.", "Your email is verified. Enter your student details to create your portal account.")}
      <div class="dashboard-panel" style="max-width:680px;margin:auto">
        <form id="registrationForm">
          <div class="form-field"><label for="studentName">Full name</label><input id="studentName" name="name" required maxlength="80" placeholder="Enter your full name"></div>
          <div class="form-field"><label for="scholarId">7-digit scholar ID</label><input id="scholarId" name="scholar_id" inputmode="numeric" pattern="[0-9]{7}" maxlength="7" required placeholder="1234567"></div>
          <div class="form-field"><label for="studentDepartment">Department</label><select id="studentDepartment" name="department" required><option value="">Select department</option>${["CSE","ECE","EE","EIE","ME","CE","Chemistry","Physics","Mathematics"].map(x=>`<option value="${x}">${x}</option>`).join("")}</select></div>
          <p class="form-note">Verified email: <b>${escapeHTML(state.studentEmail)}</b></p>
          <button class="btn btn-primary" type="submit">Complete registration →</button>
        </form>
      </div>
    </section>`;
}

// =====================================================
// STUDENT DASHBOARD
// =====================================================

function renderStudent() {
  const hasVoted = Object.keys(state.votes).length;
  app.innerHTML = `
    <section class="page-shell">
      ${pageHead("04 — STUDENT DASHBOARD", "Your campus. Your choice.", `Welcome, ${state.student?.name || state.studentEmail}.`)}
      <div class="dashboard-grid">
        <div class="dashboard-panel"><h2>Election status</h2><span class="status-pill">${escapeHTML(state.electionStatus.toUpperCase())}</span><p class="form-note">${state.electionStatus === "ongoing" ? "Voting is open. Cast your vote for each available position." : "Voting is currently not open."}</p><button class="btn btn-primary" data-page="vote" ${state.electionStatus !== "ongoing" ? "disabled" : ""}>Go to voting →</button></div>
        <div class="dashboard-panel"><h2>Your voting activity</h2><p class="form-note">${hasVoted ? `${hasVoted} position(s) have a recorded vote.` : "You have not voted yet."}</p><button class="btn btn-outline" data-page="candidates">Review candidates</button></div>
      </div>
      <div class="warning-note">Your ballot is stored in the SQLite database. The database enforces one vote per student per position.</div>
      <button class="btn btn-outline" data-student-logout>Sign out</button>
    </section>`;
}

// =====================================================
// VOTING PAGE
// =====================================================

function renderVote() {
  app.innerHTML = `
    <section class="page-shell">
      ${pageHead("05 — CAST YOUR VOTE", "Choose by position.", "Select one candidate for each position. Your vote is submitted to the server and cannot be changed.")}
      ${POSITIONS.map(p => { const candidates=state.candidates.filter(c=>c.position===p.code); const chosen=state.votes[p.code]; return `<div class="dashboard-panel"><h2>${p.code} — ${escapeHTML(p.name)}</h2><p class="form-note">${escapeHTML(p.desc)}</p>${candidates.length ? candidates.map(c=>`<div class="vote-option"><div><strong>${escapeHTML(c.name)}</strong><small>${escapeHTML(c.department||"")}</small></div><button class="btn ${Number(chosen)===c.id?"btn-outline":"btn-primary"}" data-vote="${c.id}" ${state.electionStatus!=="ongoing"||chosen?"disabled":""}>${Number(chosen)===c.id?"Voted ✓":chosen?"Vote recorded":"Vote"}</button></div>`).join("") : `<p class="form-note">No candidates listed for this position.</p>`}</div>`; }).join("")}
      <div class="warning-note">Votes are saved in SQLite. Once submitted, a vote for a position cannot be changed.</div>
    </section>`;
}

// =====================================================
// RESULTS PAGE
// =====================================================

function renderResults() {
  const available=state.published && state.electionStatus==="completed";
  app.innerHTML = `<section class="page-shell">${pageHead("06 — ELECTION OUTCOME", "The results.", "Published results calculated from the votes stored in SQLite.")}${available ? POSITIONS.map(p=>{const rows=state.results.filter(x=>x.position===p.code).sort((a,b)=>b.votes-a.votes);const total=rows.reduce((n,x)=>n+x.votes,0);return `<div class="dashboard-panel"><h2>${p.code} — ${escapeHTML(p.name)}</h2>${rows.length?rows.map(c=>`<div class="result-row"><div style="flex:1"><strong>${escapeHTML(c.name)}</strong><div class="progress-track"><div class="progress-fill" style="width:${total?c.votes/total*100:0}%"></div></div></div><b>${c.votes} vote(s)</b></div>`).join(""):`<p class="form-note">No candidates.</p>`}</div>`;}).join(""):`<div class="dashboard-panel"><h2>Results not yet published</h2><p class="form-note">The administrator must stop the election and publish results before they are visible.</p><span class="status-pill">${escapeHTML(state.electionStatus.toUpperCase())}</span></div>`}<div class="warning-note">Results are read from the backend database. This project remains a prototype and is not certified for official election use.</div></section>`;
}

// =====================================================
// CANDIDATE FORM
// =====================================================

function candidateForm(candidate = null) {
  return `
    <div class="dashboard-panel"
      ${candidate ? 'id="editCandidatePanel"' : ""}>

      <h2>
        ${candidate ? "Edit candidate" : "Add a candidate"}
      </h2>

      <form
        id="${candidate ? "editCandidateForm" : "addCandidateForm"}"
        ${candidate ? `data-id="${candidate.id}"` : ""}>

        <div class="form-field">

          <label>Candidate name</label>

          <input
            name="name"
            value="${escapeHTML(candidate?.name || "")}"
            required
          >

        </div>

        <div class="form-field">

          <label>Position</label>

          <select name="position" required>

            <option value="">Select position</option>

            ${POSITIONS.map(p => `
              <option
                value="${p.code}"
                ${
                  candidate?.position === p.code
                    ? "selected"
                    : ""
                }>

                ${p.code} — ${escapeHTML(p.name)}

              </option>
            `).join("")}

          </select>

        </div>

        <div class="form-field">

          <label>Department</label>

          <input
            name="department"
            value="${escapeHTML(candidate?.department || "")}"
            placeholder="e.g. CSE"
            required
          >

        </div>

        <div class="form-field">

          <label>Manifesto — one point per line</label>

          <textarea name="manifesto" required>${escapeHTML(candidate?.manifesto || "")}</textarea>

        </div>

        <div class="form-field">

          <label>Achievements — one point per line</label>

          <textarea name="achievements">${escapeHTML(candidate?.achievements || "")}</textarea>

        </div>

        <div class="admin-actions">

          <button class="btn btn-primary" type="submit">
            ${candidate ? "Save changes" : "Add candidate +"}
          </button>

          ${
            candidate
              ? `<button class="btn btn-outline" type="button" data-cancel-edit>Cancel</button>`
              : ""
          }

        </div>

      </form>

    </div>`;
}

// =====================================================
// ADMIN PAGE
// =====================================================

function renderAdmin() {
  const editing = state.candidates.find(
    c => c.id === state.editingId
  );

  app.innerHTML = `
    <section class="page-shell">

      ${pageHead(
        "07 — ADMINISTRATION",
        "Admin dashboard.",
        "Manage candidate information and control the election."
      )}

      <div class="dashboard-grid">

        <div class="dashboard-panel">

          <h2>Election controls</h2>

          <p class="form-note">
            Current status:
            <b>${escapeHTML(state.electionStatus)}</b>
          </p>

          <div class="admin-actions">

            <button class="btn btn-primary"
              data-admin-action="start">
              Start election
            </button>

            <button class="btn btn-outline"
              data-admin-action="stop">
              Stop election
            </button>

            <button class="btn btn-dark"
              data-admin-action="publish">
              Publish results
            </button>

          </div>

        </div>

        <div class="dashboard-panel">

          <h2>Backend connection</h2>

          <p class="form-note">
            ${
              state.backendConnected
                ? "Connected to the C++ candidate API."
                : "Not connected. Candidate sample data is being shown."
            }
          </p>

          <button class="btn btn-outline"
            data-refresh-candidates>
            Refresh candidates
          </button>

        </div>

      </div>

      ${candidateForm()}

      ${editing ? candidateForm(editing) : ""}

      <div class="dashboard-panel">

        <h2>
          Manage candidates (${state.candidates.length})
        </h2>

        ${
          state.candidates.map(c => `
            <div class="result-row">

              <span>
                <b>${escapeHTML(c.position)}</b> ·
                ${escapeHTML(c.name)}
              </span>

              <div class="admin-actions">

                <button class="btn btn-outline"
                  data-edit="${c.id}">
                  Edit
                </button>

                <button class="btn btn-outline"
                  data-remove="${c.id}">
                  Remove
                </button>

              </div>

            </div>
          `).join("")
        }

      </div>

      <div class="warning-note">
        Administrator actions are authenticated by the backend. Keep this prototype on a trusted local network.
      </div>
      <button class="btn btn-outline" data-admin-logout>Sign out</button>

    </section>`;
}

// =====================================================
// MAIN RENDER FUNCTION
// =====================================================

function render() {
  switch (state.page) {

    case "home":
      return renderHome();

    case "positions":
      return renderPositions();

    case "candidates":
      return renderCandidates();

    case "profile":
      return renderProfile();

    case "portal":
      return renderPortal();

    case "student":
      return renderStudent();

    case "vote":
      return renderVote();

    case "results":
      return renderResults();

    case "admin":
      return renderAdmin();

    default:
      return setPage("home");
  }
}

// =====================================================
// NAVIGATION AND PAGE ACTIONS
// =====================================================

document.addEventListener("click", async event => {

  const pageLink = event.target.closest("[data-page]");

  if (pageLink) {
    event.preventDefault();
    const nextPage = pageLink.dataset.page;
    if (nextPage === "portal") {
      await openPortal();
      return;
    }
    if (nextPage === "results") {
      try { const data = await apiRequest("/results"); state.results=data.results || []; await loadElection(); }
      catch(error) { state.results=[]; try { await loadElection(); } catch {} }
    }
    if (nextPage === "student" && state.studentToken) { try { await loadStudentData(); await loadElection(); } catch(error) { toast(error.message); } }
    if (nextPage === "vote") { try { await loadStudentData(); await loadElection(); } catch(error) { toast(error.message); return; } }
    setPage(nextPage);
    return;
  }

  const filter = event.target.closest("[data-filter]");

  if (filter) {
    state.filter = filter.dataset.filter;
    renderCandidates();
    return;
  }

  const profile = event.target.closest("[data-profile]");

  if (profile) {
    state.selected = Number(profile.dataset.profile);
    setPage("profile");
    return;
  }

  const voteButton = event.target.closest("[data-vote]");

  if (voteButton) {

    const candidate = state.candidates.find(
      c => c.id === Number(voteButton.dataset.vote)
    );

    if (!candidate) {
      return toast("Candidate not found.");
    }

    if (!state.studentToken) return toast("Please sign in as a student first.");
    if (state.electionStatus !== "ongoing") return toast("Voting is not open.");
    if (state.votes[candidate.position]) return toast("A vote is already recorded for this position.");
    try {
      await apiRequest("/vote", { method: "POST", body: JSON.stringify({ candidate_id: candidate.id }) });
      state.votes[candidate.position] = candidate.id;
      toast("Your vote has been recorded.");
      renderVote();
    } catch (error) { toast(error.message); }
    return;
  }

  const adminAction = event.target.closest("[data-admin-action]");
  if (adminAction) {
    const action = adminAction.dataset.adminAction;
    try {
      await apiRequest("/admin/election", { method: "POST", body: JSON.stringify({ action }) });
      await loadElection();
      toast(action === "start" ? "Election started." : action === "stop" ? "Election stopped." : "Results published.");
      renderAdmin();
    } catch (error) { toast(error.message); }
    return;
  }

  if (event.target.closest("[data-student-logout]")) {
    sessionStorage.removeItem("nits_student_token"); state.studentToken=""; state.student=null; state.votes={}; setPage("portal"); return;
  }

  if (event.target.closest("[data-admin-logout]")) {
    sessionStorage.removeItem("nits_admin_token");
    state.adminToken = "";
    state.editingId = null;
    updatePortalButton();
    setPage("portal");
    return;
  }

  const edit = event.target.closest("[data-edit]");

  if (edit) {
    state.editingId = Number(edit.dataset.edit);

    renderAdmin();

    document.getElementById("editCandidatePanel")
      ?.scrollIntoView({ behavior: "smooth" });

    return;
  }

  if (event.target.closest("[data-cancel-edit]")) {
    state.editingId = null;
    renderAdmin();
    return;
  }

  const remove = event.target.closest("[data-remove]");

  if (remove) {

    const id = Number(remove.dataset.remove);

    const candidate = state.candidates.find(c => c.id === id);

    if (!candidate) return;

    if (!state.backendConnected) {
      return toast("Connect the backend before changing candidate records.");
    }

    if (!confirm(`Delete ${candidate.name}?`)) return;

    try {

      await apiRequest(`/candidates/${id}`, {
        method: "DELETE"
      });

      state.editingId = null;

      await loadCandidates();

      toast("Candidate deleted.");

      renderAdmin();

    } catch (error) {
      toast(error.message);
    }

    return;
  }

  if (event.target.closest("[data-refresh-candidates]")) {

    const ok = await loadCandidates();

    toast(
      ok
        ? "Candidate list refreshed."
        : "Could not connect to backend."
    );

    renderAdmin();
  }

});

// =====================================================
// FORM SUBMISSIONS
// =====================================================

document.addEventListener("submit", async event => {

  const form = event.target;

  // ---------------------------------------------------
  // STUDENT: SEND OTP
  // ---------------------------------------------------

  if (form.id === "studentForm") {

    event.preventDefault();

    const email = new FormData(form)
      .get("email")
      .trim()
      .toLowerCase();

    if (!/^[a-z0-9._%+-]+@(?:[a-z0-9-]+\.)*nits\.ac\.in$/i.test(email)) {
      return toast("Use your NITS student email ending in nits.ac.in.");
    }

    const button = document.getElementById("sendOtpButton");

    button.disabled = true;
    button.textContent = "Sending OTP...";

    try {

      const data = await apiRequest("/send-otp", {
        method: "POST",
        body: JSON.stringify({ email })
      });

      state.studentEmail = email;
      state.otpSent = true;

      toast(data.message || "OTP sent successfully. Check your email.");

      renderPortal();

    } catch (error) {

      toast(error.message);

    } finally {

      const sendButton = document.getElementById("sendOtpButton");

      if (sendButton) {
        sendButton.disabled = false;
        sendButton.textContent = "Send OTP →";
      }

    }

    return;
  }

  // ---------------------------------------------------
  // STUDENT: VERIFY OTP
  // ---------------------------------------------------

  if (form.id === "verifyOtpForm") {

    event.preventDefault();

    const otp = document
      .getElementById("studentOtp")
      .value.trim();

    if (!/^[0-9]{6}$/.test(otp)) {
      return toast("Enter a valid six-digit OTP.");
    }

    const button = document.getElementById("verifyOtpButton");

    button.disabled = true;
    button.textContent = "Verifying...";

    try {

      const data = await apiRequest("/verify-otp", {
        method: "POST",
        body: JSON.stringify({
          email: state.studentEmail,
          otp: otp
        })
      });

      if (data.verified !== true) {
        throw new Error(data.message || "OTP verification failed.");
      }

      state.otpSent = false;
      if (data.registered && data.token) {
        state.studentToken=data.token; state.adminToken=""; sessionStorage.removeItem("nits_admin_token"); sessionStorage.setItem("nits_student_token",data.token); updatePortalButton();
        await loadStudentData(); await loadElection();
        toast("Welcome back. Email verified successfully."); setPage("student");
      } else {
        state.verificationToken=data.verification_token;
        toast("Email verified. Complete your student registration.");
        renderRegistration();
      }

    } catch (error) {

      toast(error.message);

      button.disabled = false;
      button.textContent = "Verify OTP →";

    }

    return;
  }

  // ---------------------------------------------------
  // ADMIN LOGIN
  // ---------------------------------------------------

  if (form.id === "adminForm") {

    event.preventDefault();

    const password = new FormData(form).get("password");
    try {
      const data = await apiRequest("/admin/login", { method: "POST", body: JSON.stringify({ password }) });
      state.adminToken=data.token; state.studentToken=""; sessionStorage.removeItem("nits_student_token"); sessionStorage.setItem("nits_admin_token",data.token); updatePortalButton();
      await loadElection(); toast("Administrator signed in."); setPage("admin");
    } catch(error) { toast(error.message); }

    return;
  }

  if (form.id === "registrationForm") {
    event.preventDefault();
    const values = Object.fromEntries(new FormData(form).entries());
    try {
      const data = await apiRequest("/register", { method: "POST", headers: { Authorization: `Bearer ${state.verificationToken}` }, body: JSON.stringify(values) });
      state.studentToken=data.token; state.adminToken=""; sessionStorage.removeItem("nits_admin_token"); sessionStorage.setItem("nits_student_token",data.token); state.verificationToken=""; updatePortalButton();
      await loadStudentData(); await loadElection(); toast("Student registration completed."); setPage("student");
    } catch(error) { toast(error.message); }
    return;
  }

  // ---------------------------------------------------
  // ADD / EDIT CANDIDATE
  // ---------------------------------------------------

  if (
    form.id === "addCandidateForm" ||
    form.id === "editCandidateForm"
  ) {

    event.preventDefault();

    if (!state.backendConnected) {
      return toast("Backend is not connected. Start the C++ server first.");
    }

    const data = new FormData(form);

    const candidate = {
      name: String(data.get("name") || "").trim(),
      position: String(data.get("position") || "").trim(),
      department: String(data.get("department") || "").trim(),
      manifesto: lines(data.get("manifesto")).join("\n"),
      achievements: lines(data.get("achievements")).join("\n")
    };

    if (
      !candidate.name ||
      !candidate.position ||
      !candidate.department ||
      !candidate.manifesto
    ) {
      return toast("Fill in all required candidate details.");
    }

    if (!POSITIONS.some(p => p.code === candidate.position)) {
      return toast("Select a valid position.");
    }

    const button = form.querySelector('[type="submit"]');

    button.disabled = true;
    button.textContent = "Saving...";

    try {

      if (form.id === "editCandidateForm") {

        const id = Number(form.dataset.id);

        await apiRequest(`/candidates/${id}`, {
          method: "PUT",
          body: JSON.stringify(candidate)
        });

        state.editingId = null;

        toast("Candidate updated in SQLite.");

      } else {

        await apiRequest("/candidates", {
          method: "POST",
          body: JSON.stringify(candidate)
        });

        form.reset();

        toast("Candidate added to SQLite.");
      }

      await loadCandidates();

      renderAdmin();

    } catch (error) {

      toast(error.message);

      button.disabled = false;

      button.textContent =
        form.id === "editCandidateForm"
          ? "Save changes"
          : "Add candidate +";

    }

    return;
  }

});

// =====================================================
// MOBILE MENU
// =====================================================

document.getElementById("menuToggle").addEventListener("click", () => {
  document.getElementById("mainNav").classList.toggle("open");
});

// =====================================================
// KEYBOARD SUPPORT
// =====================================================

document.addEventListener("keydown", event => {

  if (
    (event.key === "Enter" || event.key === " ") &&
    event.target.matches(".position-card")
  ) {

    event.preventDefault();

    state.filter = event.target.dataset.position;

    setPage("candidates");
  }

});

// =====================================================
// START APPLICATION
// =====================================================

async function startApp() {

  render();
  updatePortalButton();

  const connected = await loadCandidates();
  try { await loadElection(); } catch (error) { console.warn(error.message); }
  if (state.studentToken) {
    try { await loadStudentData(); state.page="student"; } catch { sessionStorage.removeItem("nits_student_token"); state.studentToken=""; }
  }
  if (state.adminToken) {
    try { await apiRequest("/election"); state.page="admin"; } catch { sessionStorage.removeItem("nits_admin_token"); state.adminToken=""; }
  }
  updatePortalButton();
  if (connected || state.studentToken || state.adminToken) render();

}

startApp();