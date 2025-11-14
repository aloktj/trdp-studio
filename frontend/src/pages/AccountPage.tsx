import { useAuth } from '../context/AuthContext';

export function AccountPage() {
  const { user, logout } = useAuth();

  return (
    <div>
      <h1>Account</h1>
      <section className="card">
        <p>
          <strong>Username:</strong> {user?.username}
        </p>
        <p>
          <strong>Role:</strong> {user?.role}
        </p>
        <button type="button" onClick={logout}>
          Log out
        </button>
      </section>
    </div>
  );
}
