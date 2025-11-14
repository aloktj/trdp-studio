import { createContext, useContext, useEffect, useState } from 'react';
import type { ReactNode } from 'react';
import { apiGet, apiPost } from '../api/client';
import type { UserInfo } from '../types';

interface AuthContextValue {
  user: UserInfo | null;
  login: (username: string, password: string) => Promise<void>;
  logout: () => Promise<void>;
}

const AuthContext = createContext<AuthContextValue | undefined>(undefined);

async function fetchProfile(): Promise<UserInfo | null> {
  try {
    const data = await apiGet<{ user: UserInfo }>('/api/account/profile');
    return data.user;
  } catch (err) {
    console.warn('Unable to load profile', err);
    return null;
  }
}

export function AuthProvider({ children }: { children: ReactNode }) {
  const [user, setUser] = useState<UserInfo | null>(null);

  useEffect(() => {
    const stored = window.sessionStorage.getItem('trdp-user');
    if (stored) {
      try {
        setUser(JSON.parse(stored));
      } catch (err) {
        window.sessionStorage.removeItem('trdp-user');
      }
    } else {
      fetchProfile().then((profile) => {
        if (profile) {
          setUser(profile);
        }
      });
    }
  }, []);

  useEffect(() => {
    if (user) {
      window.sessionStorage.setItem('trdp-user', JSON.stringify(user));
    } else {
      window.sessionStorage.removeItem('trdp-user');
    }
  }, [user]);

  const login = async (username: string, password: string) => {
    await apiPost<{ status: string }>("/api/auth/login", { username, password });
    const profile = await fetchProfile();
    setUser(profile ?? { username, role: 'dev' });
  };

  const logout = async () => {
    await apiPost<{ status: string }>("/api/auth/logout");
    setUser(null);
  };

  return <AuthContext.Provider value={{ user, login, logout }}>{children}</AuthContext.Provider>;
}

export function useAuth() {
  const ctx = useContext(AuthContext);
  if (!ctx) {
    throw new Error('useAuth must be used inside AuthProvider');
  }
  return ctx;
}
