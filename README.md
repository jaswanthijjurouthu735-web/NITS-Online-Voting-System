# NITS Online Voting System

A web-based online voting portal developed as a project for NIT Silchar.

## Features

- Student email OTP authentication
- Candidate management
- Candidate manifestos and achievements
- Online voting for different Gymkhana positions
- Prevention of multiple votes in the same category
- Election management and results

## Tech Stack

- Frontend: HTML, CSS, JavaScript
- Backend: C++ with Crow
- Database: SQLite
- Email authentication: Brevo API

## Project Setup

1. Clone this repository.
2. Create your environment file:

   cp .env.example .env

3. Add your own Brevo API key and admin password to `.env`.
4. Compile the backend:

   g++ -std=c++17 -O2 backend/main.cpp -o backend/server_linux -lsqlite3 -lcurl -pthread

5. Start the backend:

   ./backend/server_linux

6. Serve the `frontend` folder using a local development server.

## Environment Variables

- `BREVO_API_KEY`: Your Brevo API key.
- `ADMIN_PASSWORD`: Your chosen admin password.

Never publish your actual `.env` file or credentials.

## Disclaimer

This is an educational project and prototype. It has not been certified or audited for use in official elections.
