import { BrowserRouter, Navigate, Route, Routes } from 'react-router-dom';
import { AuthProvider } from './context/AuthContext';
import { ProtectedRoute } from './components/ProtectedRoute';
import { AppLayout } from './components/AppLayout';
import { LoginPage } from './pages/LoginPage';
import { TrdpConfigPage } from './pages/TrdpConfigPage';
import { NetworkPage } from './pages/NetworkPage';
import { TrdpCommPage } from './pages/TrdpCommPage';
import { LogsPage } from './pages/LogsPage';
import { AccountPage } from './pages/AccountPage';
import { HelpPage } from './pages/HelpPage';

function App() {
  return (
    <BrowserRouter>
      <AuthProvider>
        <Routes>
          <Route path="/login" element={<LoginPage />} />
          <Route element={<ProtectedRoute />}>
            <Route element={<AppLayout />}>
              <Route path="/" element={<Navigate to="/trdp/config" replace />} />
              <Route path="/trdp/config" element={<TrdpConfigPage />} />
              <Route path="/network" element={<NetworkPage />} />
              <Route path="/trdp/comm" element={<TrdpCommPage />} />
              <Route path="/logs" element={<LogsPage />} />
              <Route path="/account" element={<AccountPage />} />
              <Route path="/help" element={<HelpPage />} />
            </Route>
          </Route>
          <Route path="*" element={<Navigate to="/trdp/config" replace />} />
        </Routes>
      </AuthProvider>
    </BrowserRouter>
  );
}

export default App;
