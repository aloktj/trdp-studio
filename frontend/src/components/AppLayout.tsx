import { NavLink, Outlet } from 'react-router-dom';
import { useAuth } from '../context/AuthContext';

const navItems = [
  { path: '/trdp/config', label: 'TRDP Config' },
  { path: '/network', label: 'Network' },
  { path: '/trdp/comm', label: 'TRDP Comms' },
  { path: '/logs', label: 'Logs' },
  { path: '/account', label: 'Account Control' },
  { path: '/help', label: 'Help' },
];

export function AppLayout() {
  const { user } = useAuth();

  return (
    <div className="app-shell">
      <aside className="sidebar">
        <div className="brand">TRDP Studio</div>
        <div className="user-info">
          <div>{user?.username ?? 'Anonymous'}</div>
          <small>{user?.role ?? 'role unknown'}</small>
        </div>
        <nav>
          <ul>
            {navItems.map((item) => (
              <li key={item.path}>
                <NavLink to={item.path} className={({ isActive }) => (isActive ? 'active' : undefined)}>
                  {item.label}
                </NavLink>
              </li>
            ))}
          </ul>
        </nav>
      </aside>
      <main className="content">
        <Outlet />
      </main>
    </div>
  );
}
