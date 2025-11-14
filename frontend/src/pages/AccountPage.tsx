import { useCallback, useEffect, useState } from 'react';
import type { FormEvent } from 'react';
import { apiGet, apiPost } from '../api/client';
import { useAuth } from '../context/AuthContext';
import type { UserInfo } from '../types';

export function AccountPage() {
  const { user, logout } = useAuth();
  const [profile, setProfile] = useState<UserInfo | null>(user);
  const [loadingProfile, setLoadingProfile] = useState(false);
  const [currentPassword, setCurrentPassword] = useState('');
  const [newPassword, setNewPassword] = useState('');
  const [confirmPassword, setConfirmPassword] = useState('');
  const [passwordNotice, setPasswordNotice] = useState<string | null>(null);
  const [passwordError, setPasswordError] = useState<string | null>(null);

  const [users, setUsers] = useState<UserInfo[]>([]);
  const [loadingUsers, setLoadingUsers] = useState(false);
  const [adminError, setAdminError] = useState<string | null>(null);
  const [adminNotice, setAdminNotice] = useState<string | null>(null);
  const [resetTarget, setResetTarget] = useState<number | null>(null);
  const [resetPassword, setResetPassword] = useState('');

  useEffect(() => {
    setProfile(user);
  }, [user]);

  const loadProfile = useCallback(async () => {
    setLoadingProfile(true);
    try {
      const data = await apiGet<{ user: UserInfo }>('/api/account/me');
      setProfile(data.user);
    } catch (err) {
      console.warn('Unable to refresh profile', err);
    } finally {
      setLoadingProfile(false);
    }
  }, []);

  const loadUsers = useCallback(async () => {
    if (user?.role !== 'admin') {
      setUsers([]);
      return;
    }
    setLoadingUsers(true);
    setAdminError(null);
    setAdminNotice(null);
    try {
      const data = await apiGet<{ users: UserInfo[] }>('/api/account/users');
      setUsers(data.users);
    } catch (err) {
      setAdminError((err as Error).message ?? 'Unable to load users');
    } finally {
      setLoadingUsers(false);
    }
  }, [user?.role]);

  useEffect(() => {
    loadProfile();
  }, [loadProfile, user?.id]);

  useEffect(() => {
    if (user?.role === 'admin') {
      loadUsers();
    }
  }, [user?.role, loadUsers]);

  const handlePasswordSubmit = async (event: FormEvent) => {
    event.preventDefault();
    setPasswordError(null);
    setPasswordNotice(null);
    if (!currentPassword || !newPassword) {
      setPasswordError('Both current and new passwords are required.');
      return;
    }
    if (newPassword !== confirmPassword) {
      setPasswordError('New password entries do not match.');
      return;
    }
    try {
      await apiPost<{ status: string }>('/api/account/me/password', {
        current_password: currentPassword,
        new_password: newPassword,
      });
      setPasswordNotice('Password updated successfully.');
      setCurrentPassword('');
      setNewPassword('');
      setConfirmPassword('');
    } catch (err) {
      setPasswordError((err as Error).message ?? 'Unable to update password');
    }
  };

  const handleResetClick = (id: number) => {
    setResetTarget(id);
    setResetPassword('');
    setAdminError(null);
    setAdminNotice(null);
  };

  const handleResetSubmit = async (event: FormEvent) => {
    event.preventDefault();
    if (resetTarget == null) {
      return;
    }
    if (resetPassword.length < 8) {
      setAdminError('New password must be at least 8 characters long.');
      return;
    }
    try {
      await apiPost<{ status: string }>(`/api/account/users/${resetTarget}/reset_password`, {
        new_password: resetPassword,
      });
      setAdminNotice('Password reset successfully.');
      setResetTarget(null);
      setResetPassword('');
      await loadUsers();
    } catch (err) {
      setAdminError((err as Error).message ?? 'Unable to reset password');
    }
  };

  const selectedUser = resetTarget != null ? users.find((u) => u.id === resetTarget) : null;

  return (
    <div>
      <h1>Account Control</h1>

      <section className="card">
        <div className="card-header">
          <h2>Profile</h2>
          <button type="button" onClick={() => logout()}>
            Log out
          </button>
        </div>
        {loadingProfile && <div className="notice">Loading profile…</div>}
        {profile ? (
          <div className="stack">
            <p>
              <strong>Username:</strong> {profile.username}
            </p>
            <p>
              <strong>Role:</strong> {profile.role}
            </p>
            <p>
              <strong>Created:</strong> <span className="muted">{new Date(profile.created_at).toLocaleString()}</span>
            </p>
          </div>
        ) : (
          <div className="error">Unable to load profile information.</div>
        )}
      </section>

      <section className="card">
        <h2>Change your password</h2>
        <form className="stack" onSubmit={handlePasswordSubmit}>
          <label>
            Current password
            <input type="password" value={currentPassword} onChange={(e) => setCurrentPassword(e.target.value)} required />
          </label>
          <label>
            New password
            <input type="password" value={newPassword} onChange={(e) => setNewPassword(e.target.value)} required minLength={8} />
          </label>
          <label>
            Confirm new password
            <input type="password" value={confirmPassword} onChange={(e) => setConfirmPassword(e.target.value)} required minLength={8} />
          </label>
          {passwordError && <div className="error">{passwordError}</div>}
          {passwordNotice && <div className="notice">{passwordNotice}</div>}
          <button type="submit">Update password</button>
        </form>
      </section>

      {profile?.role === 'admin' && (
        <section className="card">
          <div className="card-header">
            <h2>Admin: manage users</h2>
            <button type="button" onClick={loadUsers} disabled={loadingUsers}>
              {loadingUsers ? 'Refreshing…' : 'Refresh'}
            </button>
          </div>
          {adminError && <div className="error">{adminError}</div>}
          {adminNotice && <div className="notice">{adminNotice}</div>}
          <div className="table-wrapper">
            <table>
              <thead>
                <tr>
                  <th>ID</th>
                  <th>Username</th>
                  <th>Role</th>
                  <th>Created</th>
                  <th>Actions</th>
                </tr>
              </thead>
              <tbody>
                {users.map((account) => (
                  <tr key={account.id}>
                    <td>{account.id}</td>
                    <td>{account.username}</td>
                    <td>{account.role}</td>
                    <td>{new Date(account.created_at).toLocaleString()}</td>
                    <td>
                      <button type="button" onClick={() => handleResetClick(account.id)}>
                        Reset password
                      </button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>

          {resetTarget != null && (
            <form className="inline-form" onSubmit={handleResetSubmit}>
              <div>
                Reset password for <strong>{selectedUser?.username ?? `user #${resetTarget}`}</strong>
              </div>
              <label>
                New password
                <input
                  type="password"
                  value={resetPassword}
                  minLength={8}
                  onChange={(e) => setResetPassword(e.target.value)}
                  required
                />
              </label>
              <div className="actions">
                <button type="submit">Confirm reset</button>
                <button type="button" onClick={() => setResetTarget(null)}>
                  Cancel
                </button>
              </div>
            </form>
          )}
        </section>
      )}
    </div>
  );
}
